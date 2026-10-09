// iOS platform services written in Swift (StoreKit 2 has no Objective-C API): Firebase (Analytics, Remote Config,
// anonymous Auth + the verifyPurchase Cloud Function; Crashlytics starts by itself) and in-app purchases.
// Objective-C++ reaches these through the generated CuckooStack-Swift.h (IOSServices.mm adapts them to src/core).
import AVFoundation
import FirebaseAnalytics
import FirebaseAuth
import FirebaseCore
import FirebaseFunctions
import FirebaseMessaging
import FirebaseRemoteConfig
import Foundation
import GameKit
import ReplayKit
import StoreKit
import UIKit
import UserNotifications

// MARK: - Firebase

@objc(CSFirebase) public final class CSFirebase: NSObject {
    @objc public static let shared = CSFirebase()
    @objc public private(set) var enabled = false

    /// Each environment has its own GoogleService-Info.plist (config/firebase/<env>/, copied in by CMake).
    /// Without it Firebase stays off and every call here is a no-op, so the game still runs.
    @objc public func start() {
        guard !enabled else { return }
        guard Bundle.main.path(forResource: "GoogleService-Info", ofType: "plist") != nil else {
            NSLog("[CuckooStack] Firebase: no GoogleService-Info.plist in this build; analytics, remote config and purchase checks are off")
            return
        }
        FirebaseApp.configure()
        enabled = true
        let rc = RemoteConfig.remoteConfig()
        let settings = RemoteConfigSettings()
        #if DEBUG
        settings.minimumFetchInterval = 60
        #else
        settings.minimumFetchInterval = 3600
        #endif
        rc.configSettings = settings
        // activated now, read by the game at the next launch (Tuning::load), so rules never change mid-session
        rc.fetchAndActivate { status, _ in NSLog("[CuckooStack] Firebase: remote config %@", status == .error ? "fetch failed" : "updated") }
    }

    /// Under-13s: no ad storage or personalisation signals from Analytics either.
    @objc public func setChild(_ child: Bool) {
        guard enabled else { return }
        Analytics.setConsent([.adStorage: child ? .denied : .granted, .adUserData: child ? .denied : .granted,
                              .adPersonalization: child ? .denied : .granted, .analyticsStorage: .granted])
        Analytics.setUserProperty(child ? "false" : "true", forName: AnalyticsUserPropertyAllowAdPersonalizationSignals)
    }

    /// kv = [key0, value0, key1, value1, ...]; numeric values are sent as numbers.
    @objc public func logEvent(_ name: String, kv: [String]) {
        guard enabled else { return }
        var params: [String: Any] = [:]
        var i = 0
        while i + 1 < kv.count {
            let v = kv[i + 1]
            if let n = Int64(v) { params[kv[i]] = n } else if let d = Double(v) { params[kv[i]] = d } else { params[kv[i]] = v }
            i += 2
        }
        Analytics.logEvent(name, parameters: params)
    }

    @objc public func logAdRevenue(format: String, unit: String, value: Double, currency: String) {
        guard enabled else { return }
        Analytics.logEvent(AnalyticsEventAdImpression, parameters: [AnalyticsParameterAdPlatform: "admob", AnalyticsParameterAdFormat: format,
                                                                     AnalyticsParameterAdUnitName: unit, AnalyticsParameterValue: value,
                                                                     AnalyticsParameterCurrency: currency])
    }

    @objc public func setUserProperty(_ name: String, value: String) {
        guard enabled else { return }
        Analytics.setUserProperty(value, forName: name)
    }

    /// NaN when the server doesn't set the key (the game keeps its compiled-in default).
    @objc public func remoteNumber(_ key: String) -> Double {
        guard enabled else { return .nan }
        let v = RemoteConfig.remoteConfig().configValue(forKey: key)
        return v.source == .static ? .nan : v.numberValue.doubleValue
    }

    // lazy anonymous sign-in: only players who buy, or share a challenge with reminders on, ever get an account
    private func signedIn(_ then: @escaping (Bool) -> Void) {
        guard enabled else { then(false); return }
        if Auth.auth().currentUser != nil { then(true); return }
        Auth.auth().signInAnonymously { _, error in
            if let error { NSLog("[CuckooStack] Firebase: anonymous sign-in failed: %@", error.localizedDescription) }
            then(error == nil)
        }
    }

    // MARK: friend nudges (firebase/functions/src/challenges.ts)
    private var challengeId: String?
    private let nudgeLock = NSLock()

    /// Firebase on and this player gets pushes (13+ with reminders on): the only case where a nudge can reach them.
    @objc public func nudgesAvailable() -> Bool { enabled && CSNotifications.shared.permission() == 1 && Messaging.messaging().fcmToken != nil }

    @objc public func createChallenge(day: String, meters: Int) {
        guard let token = Messaging.messaging().fcmToken else { return }
        signedIn { ok in
            guard ok else { return }
            Functions.functions().httpsCallable("createChallenge").call(["day": day, "meters": meters, "token": token]) { result, error in
                if let error { NSLog("[CuckooStack] Challenge: create failed (%@)", error.localizedDescription); return }
                let id = (result?.data as? [String: Any])?["id"] as? String
                self.nudgeLock.locked { self.challengeId = id }
            }
        }
    }

    @objc public func pollChallengeId() -> String? { nudgeLock.locked { let i = challengeId; challengeId = nil; return i } }

    @objc public func challengeBeaten(id: String, meters: Int) {
        signedIn { ok in
            guard ok else { return }
            Functions.functions().httpsCallable("challengeBeaten").call(["id": id, "meters": meters]) { _, error in
                if let error { NSLog("[CuckooStack] Challenge: report failed (%@)", error.localizedDescription) }
            }
        }
    }

    /// Server-side purchase check (Cloud Function verifyPurchase, which also credits the cloud wallet).
    /// true = verified; false = rejected by the server. Firebase off or unreachable -> true: the store already charged
    /// the player, and a lost connection must never lose their coins.
    @objc public func verifyPurchase(productId: String, token: String, transactionId: String, done: @escaping (Bool) -> Void) {
        guard enabled else { done(true); return }
        let call = {
            Functions.functions().httpsCallable("verifyPurchase").call(
                ["platform": "ios", "productId": productId, "token": token, "orderId": transactionId]) { result, error in
                if let error {
                    NSLog("[CuckooStack] Purchases: verify unreachable (%@); crediting anyway", error.localizedDescription)
                    done(true)
                    return
                }
                let valid = (result?.data as? [String: Any])?["valid"] as? Bool ?? false
                NSLog("[CuckooStack] Purchases: verify %@ -> %@", productId, valid ? "valid" : "rejected")
                done(valid)
            }
        }
        signedIn { ok in if ok { call() } else { done(true) } }
    }
}

// MARK: - In-app purchases (StoreKit 2)

private extension NSLock {
    // NSLocking.withLock needs iOS 16; the game supports iOS 15
    func locked<T>(_ body: () throws -> T) rethrows -> T {
        lock()
        defer { unlock() }
        return try body()
    }
}

/// Products: src/core/Economy.h kProducts. Raw transactions go to Objective-C++ as events; consumables are verified
/// there (Firebase) before the game credits them, then finished here. Non-consumables are re-reported on every launch
/// from the current entitlements, so Remove Ads survives reinstalls without a "restore" tap.
@objc(CSStore) public final class CSStore: NSObject {
    @objc public static let shared = CSStore()
    private static let ids = ["coins_s", "coins_m", "coins_l", "coins_xl", "coins_xxl", "starter_pack", "remove_ads"]

    private let lock = NSLock()
    private var products: [String: Product] = [:]
    private var transactions: [String: Transaction] = [:] // unfinished, by transaction id
    private var events: [String] = []
    private var updates: Task<Void, Never>?

    @objc public func start() {
        guard updates == nil else { return }
        updates = Task.detached { [weak self] in
            for await r in Transaction.updates { await self?.handle(r, restored: false) }
        }
        Task {
            do {
                let list = try await Product.products(for: Self.ids)
                lock.locked { for p in list { products[p.id] = p } }
                NSLog("[CuckooStack] Purchases: %d products", list.count)
            } catch {
                NSLog("[CuckooStack] Purchases: product query failed: %@", error.localizedDescription)
            }
            for await r in Transaction.unfinished { await handle(r, restored: false) }
            for await r in Transaction.currentEntitlements { await handle(r, restored: true) }
        }
    }

    /// "id|price;id|price" (localised display prices)
    @objc public func productsString() -> String {
        lock.locked { products.values.map { "\($0.id)|\($0.displayPrice.replacingOccurrences(of: "|", with: ""))" }.joined(separator: ";") }
    }

    @objc public func purchase(_ id: String) -> Bool {
        guard let p = lock.locked({ products[id] }) else { return false }
        Task {
            do {
                switch try await p.purchase() {
                case .success(let r): await handle(r, restored: false)
                case .userCancelled: push("\(id)|cancelled")
                case .pending: push("\(id)|pending")
                @unknown default: push("\(id)|failed")
                }
            } catch {
                NSLog("[CuckooStack] Purchases: %@ failed: %@", id, error.localizedDescription)
                push("\(id)|failed")
            }
        }
        return true
    }

    @objc public func restore() {
        Task {
            try? await AppStore.sync()
            for await r in Transaction.currentEntitlements { await handle(r, restored: true) }
        }
    }

    /// Next event or nil: "tx|productId|transactionId|restored|jws" or "productId|cancelled|pending|failed".
    @objc public func pollEvent() -> String? {
        lock.locked { events.isEmpty ? nil : events.removeFirst() }
    }

    /// The game has credited it (or the server rejected it): close the transaction.
    @objc public func finish(_ transactionId: String) {
        guard let t = lock.locked({ transactions.removeValue(forKey: transactionId) }) else { return }
        Task { await t.finish() }
    }

    private func push(_ e: String) { lock.locked { events.append(e) } }

    private func handle(_ r: VerificationResult<Transaction>, restored: Bool) async {
        guard case .verified(let t) = r else {
            // StoreKit's own signature check failed: never credit
            push("\(r.unsafePayloadValue.productID)|failed")
            return
        }
        guard t.revocationDate == nil else { await t.finish(); return }
        let id = String(t.id)
        lock.locked { transactions[id] = t }
        push("tx|\(t.productID)|\(id)|\(restored ? 1 : 0)|\(r.jwsRepresentation)")
    }
}

// MARK: - Reminders + campaign pushes

/// Local reminders planned by the game (src/core/Reminders.h) and Firebase Cloud Messaging for console campaigns.
/// Only ever used for 13+ players who turned reminders on: nothing here runs until the game asks for permission.
@objc(CSNotifications) public final class CSNotifications: NSObject {
    @objc public static let shared = CSNotifications()
    fileprivate static let prefix = "cs.reminder."
    private let lock = NSLock()
    private var status = 0 // 0 unknown, 1 granted, 2 denied (cs::NotifPermission)
    private var opened = -1
    // the delegate protocols live on a private object, so the generated Objective-C header stays plain
    private lazy var delegate = NotificationDelegate(owner: self)

    /// From application(_:didFinishLaunchingWithOptions:): the delegate must be in place to see a cold-start tap.
    @objc public func start() {
        UNUserNotificationCenter.current().delegate = delegate
        refresh()
    }

    private func refresh() {
        UNUserNotificationCenter.current().getNotificationSettings { s in
            let v: Int
            switch s.authorizationStatus {
            case .authorized, .provisional, .ephemeral: v = 1
            case .denied: v = 2
            default: v = 0
            }
            self.lock.locked { self.status = v }
            if v == 1 { DispatchQueue.main.async { self.enablePush() } }
        }
    }

    @objc public func permission() -> Int { lock.locked { status } }

    @objc public func requestPermission() {
        UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .sound, .badge]) { granted, _ in
            self.lock.locked { self.status = granted ? 1 : 2 }
            if granted { DispatchQueue.main.async { self.enablePush() } }
        }
    }

    // FCM: a token only once the player opted in (auto-init is off in Info.plist), then the campaign topics
    private func enablePush() {
        guard CSFirebase.shared.enabled else { return }
        Messaging.messaging().delegate = delegate
        Messaging.messaging().isAutoInitEnabled = true
        UIApplication.shared.registerForRemoteNotifications()
    }

    /// Replaces every pending reminder with this set (parallel arrays; `at` in seconds since the epoch).
    @objc public func replaceAll(ids: [NSNumber], at: [NSNumber], titles: [String], bodies: [String]) {
        let center = UNUserNotificationCenter.current()
        center.getPendingNotificationRequests { pending in
            center.removePendingNotificationRequests(withIdentifiers: pending.map(\.identifier).filter { $0.hasPrefix(Self.prefix) })
            let now = Date().timeIntervalSince1970
            for i in 0..<min(ids.count, at.count, titles.count, bodies.count) {
                let wait = at[i].doubleValue - now
                guard wait > 60 else { continue }
                let content = UNMutableNotificationContent()
                content.title = titles[i]
                content.body = bodies[i]
                content.sound = .default
                let trigger = UNTimeIntervalNotificationTrigger(timeInterval: wait, repeats: false)
                center.add(UNNotificationRequest(identifier: Self.prefix + ids[i].stringValue, content: content, trigger: trigger))
            }
        }
    }

    /// The reminder the app was opened from, once; -1 if none.
    @objc public func consumeOpened() -> Int {
        lock.locked { let o = opened; opened = -1; return o }
    }

    fileprivate func didOpen(_ value: Int) { lock.locked { opened = value } }
}

private final class NotificationDelegate: NSObject, UNUserNotificationCenterDelegate, MessagingDelegate {
    private weak var owner: CSNotifications?
    init(owner: CSNotifications) { self.owner = owner }

    func userNotificationCenter(_ center: UNUserNotificationCenter, didReceive response: UNNotificationResponse,
                                withCompletionHandler completionHandler: @escaping () -> Void) {
        let id = response.notification.request.identifier
        let p = CSNotifications.prefix
        owner?.didOpen(id.hasPrefix(p) ? Int(id.dropFirst(p.count)) ?? 0 : 0) // 0 = a campaign push
        completionHandler()
    }

    // in-game: don't interrupt a run with a banner
    func userNotificationCenter(_ center: UNUserNotificationCenter, willPresent notification: UNNotification,
                                withCompletionHandler completionHandler: @escaping (UNNotificationPresentationOptions) -> Void) {
        completionHandler([])
    }

    func messaging(_ messaging: Messaging, didReceiveRegistrationToken fcmToken: String?) {
        guard fcmToken != nil else { return }
        messaging.subscribe(toTopic: "all")
        messaging.subscribe(toTopic: "ios")
    }
}

// MARK: - Share Replay (ReplayKit clip buffering)

/// The last ~15 s of the run as a video with an end card. ReplayKit keeps a rolling buffer of the screen and the
/// app's audio while replays are on (iOS asks permission the first time); at the crash the game asks for the clip,
/// AVFoundation adds a watermark and the end card, and the system share sheet sends it anywhere.
@objc(CSReplay) public final class CSReplay: NSObject {
    @objc public static let shared = CSReplay()
    // mirrors cs::ReplayState
    private enum State: Int { case unavailable = 0, off, recording, exporting, ready, failed }
    private let lock = NSLock()
    private var enabled = false, buffering = false
    private var current = State.off
    private var clip: URL?
    private var sharedTarget: String?
    private var available = true, availableCheckedAt = 0.0 // RPScreenRecorder.isAvailable is polled every frame: cache it

    private func set(_ s: State) { lock.locked { current = s } }

    @objc public func state() -> Int {
        let now = ProcessInfo.processInfo.systemUptime
        if now - availableCheckedAt > 2 { availableCheckedAt = now; available = RPScreenRecorder.shared().isAvailable }
        guard available else { return State.unavailable.rawValue }
        return lock.locked { enabled ? current.rawValue : State.off.rawValue }
    }

    /// Buffering starts right away (iOS asks permission the first time), so a full 15 s is ready at the first crash.
    @objc public func setEnabled(_ on: Bool) {
        lock.locked { enabled = on }
        if on { startBuffering() } else { stopBuffering() }
    }

    @objc public func runStarted() {
        guard lock.locked({ enabled }) else { return }
        startBuffering()
    }

    private func startBuffering() {
        // heat: no recording while the device is already hot (the game drops quality too)
        if ProcessInfo.processInfo.thermalState.rawValue >= ProcessInfo.ThermalState.serious.rawValue { stopBuffering(); set(.failed); return }
        if buffering { set(.recording); return }
        let rec = RPScreenRecorder.shared()
        rec.isMicrophoneEnabled = false
        rec.startClipBuffering { error in
            if let error {
                NSLog("[CuckooStack] Replay: clip buffering unavailable (%@)", error.localizedDescription)
                self.buffering = false
                self.set(.failed)
                return
            }
            self.buffering = true
            self.set(.recording)
        }
    }

    private func stopBuffering() {
        guard buffering else { return }
        buffering = false
        RPScreenRecorder.shared().stopClipBuffering { _ in }
    }

    @objc public func saveClip(distance: Int, score: Int, newBest: Bool, day: String) {
        guard buffering else { return }
        set(.exporting)
        let raw = FileManager.default.temporaryDirectory.appendingPathComponent("cs-replay-raw.mp4")
        try? FileManager.default.removeItem(at: raw)
        RPScreenRecorder.shared().exportClip(to: raw, duration: 15) { error in
            if let error {
                NSLog("[CuckooStack] Replay: export failed (%@)", error.localizedDescription)
                self.set(.failed)
                return
            }
            Task {
                // the end card is a bonus: if composing fails, share the plain clip
                var url = await self.compose(raw, distance: distance, newBest: newBest, day: day)
                if url == nil, let size = (try? FileManager.default.attributesOfItem(atPath: raw.path))?[.size] as? Int, size > 10_000 { url = raw }
                self.lock.locked { self.clip = url }
                self.set(url != nil ? .ready : .failed)
            }
        }
    }

    // MARK: composition: watermark + end card, scaled to 720 px wide
    private func compose(_ src: URL, distance: Int, newBest: Bool, day: String) async -> URL? {
        let asset = AVURLAsset(url: src)
        do {
            let duration = try await asset.load(.duration)
            guard let video = try await asset.loadTracks(withMediaType: .video).first else { return nil }
            let natural = try await video.load(.naturalSize)
            let transform = try await video.load(.preferredTransform)
            let comp = AVMutableComposition()
            guard let cv = comp.addMutableTrack(withMediaType: .video, preferredTrackID: kCMPersistentTrackID_Invalid) else { return nil }
            try cv.insertTimeRange(CMTimeRange(start: .zero, duration: duration), of: video, at: .zero)
            for a in try await asset.loadTracks(withMediaType: .audio) {
                let ca = comp.addMutableTrack(withMediaType: .audio, preferredTrackID: kCMPersistentTrackID_Invalid)
                try ca?.insertTimeRange(CMTimeRange(start: .zero, duration: duration), of: a, at: .zero)
            }
            let card = CMTime(seconds: 1.8, preferredTimescale: 600)
            cv.insertEmptyTimeRange(CMTimeRange(start: duration, duration: card))
            let total = CMTimeAdd(duration, card)

            let oriented = natural.applying(transform)
            let srcSize = CGSize(width: abs(oriented.width), height: abs(oriented.height))
            let scale = 720.0 / max(srcSize.width, 1)
            let size = CGSize(width: 720, height: (srcSize.height * scale / 2).rounded() * 2)
            let vc = AVMutableVideoComposition()
            vc.renderSize = size
            vc.frameDuration = CMTime(value: 1, timescale: 30)
            let instruction = AVMutableVideoCompositionInstruction()
            instruction.timeRange = CMTimeRange(start: .zero, duration: total)
            let layer = AVMutableVideoCompositionLayerInstruction(assetTrack: cv)
            layer.setTransform(transform.concatenating(CGAffineTransform(scaleX: scale, y: scale)), at: .zero)
            instruction.layerInstructions = [layer]
            vc.instructions = [instruction]
            vc.animationTool = overlay(size: size, clipSeconds: duration.seconds, distance: distance, newBest: newBest, day: day)

            let out = FileManager.default.temporaryDirectory.appendingPathComponent("Cuckoo Stack \(distance)m.mp4")
            try? FileManager.default.removeItem(at: out)
            guard let export = AVAssetExportSession(asset: comp, presetName: AVAssetExportPresetHighestQuality) else { return nil }
            export.outputURL = out
            export.outputFileType = .mp4
            export.videoComposition = vc
            export.shouldOptimizeForNetworkUse = true
            await export.export()
            return export.status == .completed ? out : nil
        } catch {
            NSLog("[CuckooStack] Replay: compose failed (%@)", error.localizedDescription)
            return nil
        }
    }

    private func text(_ s: String, size: CGFloat, color: UIColor, weight: UIFont.Weight, y: CGFloat, width: CGFloat, glow: UIColor) -> CATextLayer {
        let t = CATextLayer()
        t.string = s
        t.font = UIFont.systemFont(ofSize: size, weight: weight)
        t.fontSize = size
        t.foregroundColor = color.cgColor
        t.alignmentMode = .center
        t.contentsScale = 2
        t.frame = CGRect(x: 0, y: y, width: width, height: size * 1.3)
        t.shadowColor = glow.cgColor
        t.shadowRadius = size * 0.25
        t.shadowOpacity = 0.9
        t.shadowOffset = .zero
        return t
    }

    private func overlay(size: CGSize, clipSeconds: Double, distance: Int, newBest: Bool, day: String) -> AVVideoCompositionCoreAnimationTool {
        let pink = UIColor(red: 1, green: 0.17, blue: 0.84, alpha: 1), cyan = UIColor(red: 0.16, green: 0.91, blue: 1, alpha: 1)
        let parent = CALayer()
        parent.frame = CGRect(origin: .zero, size: size)
        parent.isGeometryFlipped = true // top-left origin, like UIKit
        let videoLayer = CALayer()
        videoLayer.frame = parent.frame
        parent.addSublayer(videoLayer)
        // watermark through the clip
        let mark = text("CUCKOO STACK", size: 26, color: .white, weight: .black, y: size.height - 70, width: size.width, glow: pink)
        mark.opacity = 0.85
        parent.addSublayer(mark)
        // end card: fades in as the clip ends
        let card = CALayer()
        card.frame = parent.frame
        card.backgroundColor = UIColor(red: 0.03, green: 0.02, blue: 0.06, alpha: 1).cgColor
        card.opacity = 0
        let mid = size.height / 2
        card.addSublayer(text(newBest ? "NEW BEST!" : "DAILY RUN", size: 34, color: cyan, weight: .heavy, y: mid - 230, width: size.width, glow: cyan))
        card.addSublayer(text("\(distance) m", size: 120, color: .white, weight: .black, y: mid - 170, width: size.width, glow: pink))
        card.addSublayer(text("Can you beat it?", size: 48, color: .white, weight: .bold, y: mid + 10, width: size.width, glow: pink))
        card.addSublayer(text(day, size: 30, color: cyan, weight: .semibold, y: mid + 90, width: size.width, glow: cyan))
        card.addSublayer(text("Cuckoo Stack", size: 64, color: .white, weight: .black, y: mid + 200, width: size.width, glow: pink))
        card.addSublayer(text("Free on iOS & Android", size: 30, color: UIColor(white: 0.9, alpha: 1), weight: .semibold, y: mid + 290, width: size.width, glow: cyan))
        let fade = CABasicAnimation(keyPath: "opacity")
        fade.fromValue = 0
        fade.toValue = 1
        fade.beginTime = AVCoreAnimationBeginTimeAtZero + clipSeconds
        fade.duration = 0.35
        fade.fillMode = .forwards
        fade.isRemovedOnCompletion = false
        card.add(fade, forKey: "in")
        parent.addSublayer(card)
        return AVVideoCompositionCoreAnimationTool(postProcessingAsVideoLayer: videoLayer, in: parent)
    }

    // MARK: share
    /// With a clip: the video plus the caption. Without one (no recording, or it failed): the caption and link alone.
    @objc public func share(caption: String, url: String) {
        let clip = lock.locked({ current == .ready ? self.clip : nil })
        DispatchQueue.main.async {
            var items: [Any] = ["\(caption) \(url)"]
            if let clip { items.insert(clip, at: 0) }
            let sheet = UIActivityViewController(activityItems: items, applicationActivities: nil)
            sheet.completionWithItemsHandler = { type, completed, _, _ in
                if completed { self.lock.locked { self.sharedTarget = type?.rawValue ?? "unknown" } }
            }
            guard let root = UIApplication.shared.connectedScenes.compactMap({ ($0 as? UIWindowScene)?.keyWindow }).first?.rootViewController else { return }
            var top = root
            while let p = top.presentedViewController { top = p }
            sheet.popoverPresentationController?.sourceView = top.view // iPad
            sheet.popoverPresentationController?.sourceRect = CGRect(x: top.view.bounds.midX, y: top.view.bounds.maxY - 120, width: 1, height: 1)
            top.present(sheet, animated: true)
        }
    }

    /// The share sheet finished with a target (e.g. com.burbn.instagram.shareextension), once; nil otherwise.
    @objc public func consumeShared() -> String? {
        lock.locked { let t = sharedTarget; sharedTarget = nil; return t }
    }
}

// MARK: - Daily leaderboards (Game Center)

/// A recurring daily board plus an all-time board (config/store/leaderboards.env). Sign-in happens on the first
/// submit or tap (13+ players only: the game never calls this for children), never as a launch prompt.
@objc(CSLeaderboards) public final class CSLeaderboards: NSObject {
    @objc public static let shared = CSLeaderboards()
    private var daily = "", allTime = ""
    private var started = false
    private var signInSheet: UIViewController?
    private var showWhenReady = false
    private lazy var dismisser = GameCenterDismisser()

    @objc public func configure(daily: String, allTime: String) { self.daily = daily; self.allTime = allTime }

    private func authenticate() {
        guard !started else { return }
        started = true
        GKLocalPlayer.local.authenticateHandler = { [weak self] sheet, error in
            guard let self else { return }
            if let error { NSLog("[CuckooStack] Game Center: %@", error.localizedDescription) }
            self.signInSheet = sheet
            if sheet != nil, self.showWhenReady { self.showWhenReady = false; self.present(sheet!) }
            else if GKLocalPlayer.local.isAuthenticated, self.showWhenReady { self.showWhenReady = false; self.show() }
        }
    }

    @objc public func submit(_ meters: Int) {
        authenticate()
        guard GKLocalPlayer.local.isAuthenticated, !daily.isEmpty else { return }
        GKLeaderboard.submitScore(meters, context: 0, player: GKLocalPlayer.local, leaderboardIDs: [daily, allTime].filter { !$0.isEmpty }) { error in
            if let error { NSLog("[CuckooStack] Game Center: submit failed (%@)", error.localizedDescription) }
        }
    }

    @objc public func show() {
        authenticate()
        if !GKLocalPlayer.local.isAuthenticated {
            if let sheet = signInSheet { present(sheet) } else { showWhenReady = true }
            return
        }
        let vc = GKGameCenterViewController(leaderboardID: daily, playerScope: .global, timeScope: .today)
        vc.gameCenterDelegate = dismisser
        present(vc)
    }

    private func present(_ vc: UIViewController) {
        DispatchQueue.main.async {
            guard var top = UIApplication.shared.connectedScenes.compactMap({ ($0 as? UIWindowScene)?.keyWindow }).first?.rootViewController else { return }
            while let p = top.presentedViewController { top = p }
            top.present(vc, animated: true)
        }
    }
}

private final class GameCenterDismisser: NSObject, GKGameCenterControllerDelegate {
    func gameCenterViewControllerDidFinish(_ gameCenterViewController: GKGameCenterViewController) {
        gameCenterViewController.dismiss(animated: true)
    }
}
