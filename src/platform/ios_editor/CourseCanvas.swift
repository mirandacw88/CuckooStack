// The course, drawn as the editor's core describes it (CourseScene: walls, barriers, floating blocks, gauntlets and
// their grids, every hazard by its registry glyph, the checker's notes, the meter's walk, the bot's run), in the
// zone's light, with its saws spinning and its disco balls twinkling. One finger (or the Pencil) uses the tool; two
// pan and pinch; a trackpad or mouse wheel scrolls. With a Pencil about, only the Pencil draws and fingers pan, and a
// hovering Pencil shows where it would land.
import SwiftUI
import UIKit

/// one finger (or the Pencil) down: the tool's gesture. The Pencil's starts at once (no slop: it lands where it
/// touches); a finger's waits a moment (80 ms, or till it's moved 8 points), so a second finger landing a beat later
/// is a pan or a pinch, never an edit. A second finger after it started cancels it.
final class ToolGestureRecognizer: UIGestureRecognizer {
    private(set) var location: CGPoint = .zero
    private(set) var startLocation: CGPoint = .zero   // where it touched down (a tap, or the wait: the tool starts there)
    private(set) var touchType: UITouch.TouchType = .direct
    private var tracked: UITouch?
    private var waiting = false
    private var timer: Timer?
    static let fingerWait: TimeInterval = 0.08
    static let fingerSlop: CGFloat = 8

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent) {
        if tracked != nil || (event.allTouches?.count ?? 0) > 1 { // (a second finger: the pan's, or the pinch's)
            if state == .began || state == .changed { state = .cancelled } else { state = .failed }
            return
        }
        guard let t = touches.first else { return }
        tracked = t
        touchType = t.type
        location = t.location(in: view)
        startLocation = location
        if t.type == .pencil { state = .began; return }
        waiting = true
        timer = Timer.scheduledTimer(withTimeInterval: Self.fingerWait, repeats: false) { [weak self] _ in
            guard let self, self.waiting, self.state == .possible else { return }
            self.waiting = false
            self.state = .began
        }
    }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent) {
        guard let t = tracked, touches.contains(t) else { return }
        location = t.location(in: view)
        if waiting {
            guard hypot(location.x - startLocation.x, location.y - startLocation.y) > Self.fingerSlop else { return }
            waiting = false
            state = .began
        } else if state == .began || state == .changed {
            state = .changed
        }
    }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent) {
        guard let t = tracked, touches.contains(t) else { return }
        location = t.location(in: view)
        waiting = false
        state = .ended // (a tap quicker than the wait: begun and ended at once, at where it touched)
    }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent) { state = .cancelled }
    override func reset() {
        tracked = nil
        waiting = false
        timer?.invalidate()
        timer = nil
    }
}

/// a transparent layer of the canvas that draws what it's told to (the course, or what moves on it)
final class Painter: UIView {
    var paint: ((CGContext) -> Void)?
    override init(frame: CGRect) {
        super.init(frame: frame)
        isOpaque = false
        backgroundColor = .clear
        isUserInteractionEnabled = false
        contentMode = .redraw
        autoresizingMask = [.flexibleWidth, .flexibleHeight]
    }
    required init?(coder: NSCoder) { fatalError() }
    override func draw(_ rect: CGRect) { if let ctx = UIGraphicsGetCurrentContext() { paint?(ctx) } }
}

/// In three layers, so a frame of animation costs little: the sky (a gradient the GPU draws), the course (redrawn
/// when it or the view changes), and what moves on it (saws, disco balls, notes, the selection: redrawn each frame)
final class CourseCanvasView: UIView, UIGestureRecognizerDelegate, UIPencilInteractionDelegate {
    override class var layerClass: AnyClass { CAGradientLayer.self }
    private let glow = CAGradientLayer()
    private let courseView = Painter()
    private let motionView = Painter()
    private var skyZone = ""

    // what it draws
    var scene: CourseScene? { didSet { if oldValue == nil, let s = scene { fitHeight(s) }; setNeedsDisplay(); if oldValue == nil { UIAccessibility.post(notification: .layoutChanged, argument: nil) } } }
    var registry: Registry = Builtin.registry
    var botRun: BotGame? { didSet { courseView.setNeedsDisplay() } }
    var playTrace: [CGPoint] = [] { didSet { courseView.setNeedsDisplay() } }
    var showWalk = true, showBot = true, showNotes = true, showGrid = true
    /// bands over the course: the chunks a roll placed (labelled), a chunk's pads
    var bands: [CanvasBand] = [] { didSet { if bands != oldValue { courseView.setNeedsDisplay(); UIAccessibility.post(notification: .layoutChanged, argument: nil) } } }
    var tool: String = "wall"
    /// what the screen's overlays cover (the iPhone's bot pill over it, its tool strip under it): the street and the
    /// meter's strip kept clear of them
    var insets: UIEdgeInsets = .zero { didSet { if insets != oldValue { if let s = scene { fitHeight(s) }; setNeedsDisplay() } } }
    var hoverWorld: CGPoint? { didSet { motionView.setNeedsDisplay() } }

    // what it says
    var onPointer: ((Phase, Double, Double) -> Void)?
    var onViewport: ((Double, Double) -> Void)?   // (the left edge, the right edge), metres along
    var onErase: ((Bool) -> Void)?
    var onTapNote: ((CourseNote) -> Void)?
    var onPencilTap: ((UIPencilPreferredAction) -> Void)?   // the Pencil's double-tap, as its settings say
    var onPencilSqueeze: ((CGPoint) -> Void)?                // the Pencil Pro's squeeze: where its tip is (or the middle)

    // the view: metres along at its left edge, metres up at its street, points a metre
    private(set) var originX: Double = -1
    private var originY: Double = 0
    private(set) var scale: Double = 26
    private var pencilOnly = false
    private let toolGesture = ToolGestureRecognizer()
    private weak var pan: UIPanGestureRecognizer?
    private var link: CADisplayLink?
    private var time: Double = 0

    override init(frame: CGRect) {
        super.init(frame: frame)
        isOpaque = true
        isMultipleTouchEnabled = true
        accessibilityIdentifier = "course.canvas"
        glow.type = .radial
        layer.insertSublayer(glow, at: 0)
        for v in [courseView, motionView] { v.frame = bounds; addSubview(v) }
        courseView.paint = { [unowned self] ctx in self.drawCourse(ctx) }
        motionView.paint = { [unowned self] ctx in self.drawMotion(ctx) }
        toolGesture.addTarget(self, action: #selector(toolMoved(_:)))
        toolGesture.delegate = self
        addGestureRecognizer(toolGesture)
        let pan = UIPanGestureRecognizer(target: self, action: #selector(panned(_:)))
        pan.minimumNumberOfTouches = 2
        pan.allowedScrollTypesMask = .all
        pan.allowedTouchTypes = Self.navigationTouches
        pan.delegate = self
        addGestureRecognizer(pan)
        self.pan = pan
        let pinch = UIPinchGestureRecognizer(target: self, action: #selector(pinched(_:)))
        pinch.allowedTouchTypes = Self.navigationTouches
        pinch.delegate = self
        addGestureRecognizer(pinch)
        addGestureRecognizer(UIHoverGestureRecognizer(target: self, action: #selector(hovered(_:))))
        let pencil = UIPencilInteraction()
        pencil.delegate = self
        addInteraction(pencil)
        let tapNote = UITapGestureRecognizer(target: self, action: #selector(tapped(_:)))
        tapNote.numberOfTouchesRequired = 2
        addGestureRecognizer(tapNote)
    }
    required init?(coder: NSCoder) { fatalError() }

    override var canBecomeFirstResponder: Bool { true }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        link?.invalidate()
        link = nil
        guard window != nil else { return }
        let l = CADisplayLink(target: self, selector: #selector(animate(_:)))
        l.preferredFrameRateRange = CAFrameRateRange(minimum: 10, maximum: 20, preferred: 15)
        l.add(to: .main, forMode: .common)
        link = l
        becomeFirstResponder()
    }

    /// the saws spin and the disco balls twinkle while you work, and settle a few seconds after the last touch or edit
    /// (a display link drawing for nothing keeps an iPad warm)
    private var lastActivity = CACurrentMediaTime()
    static let settleAfter: CFTimeInterval = 4
    func woke() {
        lastActivity = CACurrentMediaTime()
        link?.isPaused = false
    }

    /// only what moves is worth a redraw: saws, disco balls, notes, a picked hazard or a selection in view
    @objc private func animate(_ l: CADisplayLink) {
        if l.timestamp - lastActivity > Self.settleAfter { l.isPaused = true; return }
        time = l.timestamp
        guard let s = scene else { return }
        let a = originX - 1, b = originX + Double(bounds.width) / scale + 1
        let moving = s.items.contains { $0.x >= a && $0.x <= b } || (showNotes && s.notes.contains { $0.x >= a && $0.x <= b }) || s.selection != nil || s.start != nil
        if moving { motionView.setNeedsDisplay() } // (only the moving layer: the course stays as drawn)
    }

    override func setNeedsDisplay() {
        super.setNeedsDisplay()
        woke() // (an edit, a pan, a zoom: it comes alive)
        updateSky()
        courseView.setNeedsDisplay()
        motionView.setNeedsDisplay()
    }

    private func updateSky() {
        let zone = scene?.zone ?? "city"
        let look = ZoneLook.of(zone)
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        if zone != skyZone {
            skyZone = zone
            (layer as? CAGradientLayer)?.colors = look.sky.map { UIColor($0).cgColor }
            glow.colors = [look.uiAccent.withAlphaComponent(0.14).cgColor, UIColor.clear.cgColor]
        }
        let r = bounds.width * 0.7
        glow.frame = CGRect(x: bounds.midX - r, y: streetY - r, width: 2 * r, height: 2 * r)
        glow.startPoint = CGPoint(x: 0.5, y: 0.5)
        glow.endPoint = CGPoint(x: 1, y: 1)
        CATransaction.commit()
    }

    // MARK: the view

    private var streetY: CGFloat { bounds.height - insets.bottom - 30 }
    func screen(_ x: Double, _ y: Double) -> CGPoint { CGPoint(x: (x - originX) * scale, y: Double(streetY) - (y - originY) * scale) }
    func world(_ p: CGPoint) -> (x: Double, y: Double) { (originX + Double(p.x) / scale, originY + (Double(streetY) - Double(p.y)) / scale) }

    private func fitHeight(_ s: CourseScene) {
        guard bounds.height > 60 else { return }
        let want = Double(s.rows + 4) * s.cell
        // its tallest wall in view, 16 m of the course across at least (a phone held upright), and no bigger than a
        // cell's 18 points (an iPad shows more of the course, not bigger crates)
        scale = max(8, min(30, Double(streetY - insets.top - 70) / want, Double(bounds.width) / 16))
        if originX < 0 { originX = -1.5 }
        report()
    }

    /// the bands the canvas draws (a roll's placed chunks, a chunk's pads) as accessibility elements: VoiceOver and the UI tests
    /// find them by name (canvas.band.<label>). Kept between asks (rebuilt when the bands or the view change), as the system wants
    private var bandElements: [UIAccessibilityElement] = []
    private var bandElementsKey = ""
    override var accessibilityElements: [Any]? {
        get { refreshBandElements(); return bandElements }
        set {}
    }

    private func refreshBandElements() {
        let key = "\(bounds.size)|\(originX)|\(scale)|\(insets.top)|\(streetY)|" + bands.map { "\($0.label)@\($0.x0)-\($0.x1)" }.joined(separator: ",")
        guard key != bandElementsKey else { return }
        bandElementsKey = key
        let top = insets.top + 48
        bandElements = bands.map { b in
            let e = UIAccessibilityElement(accessibilityContainer: self)
            e.accessibilityLabel = (b.stripped ? "Pad " : "Chunk ") + b.label
            e.accessibilityIdentifier = "canvas.band." + b.label
            // (its place on screen as of this ask)
            let a = screen(b.x0, 0).x, z = screen(b.x1, 0).x
            e.accessibilityFrameInContainerSpace = CGRect(x: a, y: top, width: max(1, z - a), height: max(1, streetY - top))
            return e
        }
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        if let s = scene, originX < 0 { fitHeight(s) }
        setNeedsDisplay()
    }

    /// the view's left edge to x (the minimap, a note tapped): centred on it
    func center(on x: Double, animated: Bool = true) {
        let target = x - Double(bounds.width) / scale / 2
        if animated {
            let from = originX
            let start = CACurrentMediaTime()
            Timer.scheduledTimer(withTimeInterval: 1 / 60, repeats: true) { [weak self] t in
                guard let self else { t.invalidate(); return }
                let k = min(1, (CACurrentMediaTime() - start) / 0.35)
                let e = 1 - pow(1 - k, 3)
                self.originX = from + (target - from) * e
                self.clampOrigin()
                self.report()
                self.setNeedsDisplay()
                if k >= 1 { t.invalidate() }
            }
        } else { originX = target; clampOrigin(); report(); setNeedsDisplay() }
    }

    func zoom(by k: Double, around p: CGPoint? = nil) {
        let at = p ?? CGPoint(x: bounds.midX, y: bounds.midY)
        let w = world(at)
        scale = max(4, min(90, scale * k))
        originX = w.x - Double(at.x) / scale
        // the street stays where it is (a pinch over the sky doesn't lose the course under the screen's edge), unless
        // it's been panned up already: then around the fingers, up and down too
        if originY > 0.01 { originY = max(0, w.y - (Double(streetY) - Double(at.y)) / scale) }
        clampOrigin()
        report()
        setNeedsDisplay()
    }

    private func clampOrigin() {
        let len = scene?.courseLength ?? 100
        originX = max(-6, min(len + 6 - Double(bounds.width) / scale, originX))
        originY = max(-2, min(30, originY))
    }

    private func report() { onViewport?(originX, originX + Double(bounds.width) / scale) }

    // MARK: gestures

    // the Pan tool: one finger (or the Pencil) drags the course along, and lets it glide on when let go
    private var panLast: (p: CGPoint, t: CFTimeInterval)?
    private var panVelocity: CGPoint = .zero
    private var glide: CADisplayLink?

    private func panTool(_ g: ToolGestureRecognizer) {
        let now = CACurrentMediaTime()
        switch g.state {
        case .began:
            glide?.invalidate(); glide = nil
            panLast = (g.startLocation, now)
            panVelocity = .zero
        case .changed:
            guard let last = panLast else { return }
            let d = CGPoint(x: g.location.x - last.p.x, y: g.location.y - last.p.y)
            let dt = max(1.0 / 240, now - last.t)
            panVelocity = CGPoint(x: d.x / dt * 0.6 + panVelocity.x * 0.4, y: d.y / dt * 0.6 + panVelocity.y * 0.4)
            panLast = (g.location, now)
            scroll(by: d)
        case .ended:
            panLast = nil
            if abs(panVelocity.x) > 60 || abs(panVelocity.y) > 60 {
                let l = CADisplayLink(target: self, selector: #selector(glideStep(_:)))
                l.add(to: .main, forMode: .common)
                glide = l
            }
        default: panLast = nil
        }
    }

    @objc private func glideStep(_ l: CADisplayLink) {
        let dt = l.targetTimestamp - l.timestamp
        scroll(by: CGPoint(x: panVelocity.x * dt, y: panVelocity.y * dt))
        let k = pow(0.04, dt) // (it slows to a stop in about a second)
        panVelocity = CGPoint(x: panVelocity.x * k, y: panVelocity.y * k)
        if abs(panVelocity.x) < 8 && abs(panVelocity.y) < 8 { l.invalidate(); glide = nil }
    }

    private func scroll(by d: CGPoint) {
        originX -= Double(d.x) / scale
        originY += Double(d.y) / scale
        clampOrigin()
        report()
        setNeedsDisplay()
    }

    private var toolBegun = false

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) { woke(); super.touchesBegan(touches, with: event) }

    @objc private func toolMoved(_ g: ToolGestureRecognizer) {
        if g.touchType == .pencil && !pencilOnly && traitCollection.userInterfaceIdiom == .pad { usePencilOnly(true) }
        if tool == "pan" { panTool(g); return }
        let w = world(g.location), start = world(g.startLocation)
        switch g.state {
        case .began: // (where it touched down, then where it is: a finger's waited a moment)
            toolBegun = true
            onPointer?(.begin, start.x, start.y)
            if g.location != g.startLocation { onPointer?(.move, w.x, w.y) }
        case .changed: onPointer?(.move, w.x, w.y); autoScroll(g.location)
        case .ended:
            if !toolBegun { onPointer?(.begin, start.x, start.y) } // (a tap quicker than the wait)
            onPointer?(.end, w.x, w.y)
            toolBegun = false
        case .cancelled, .failed:
            if toolBegun { onPointer?(.cancel, w.x, w.y) }
            toolBegun = false
        default: break
        }
    }

    /// iPad with a Pencil: it draws, fingers pan (one or two) and pinch
    func usePencilOnly(_ on: Bool) {
        pencilOnly = on
        toolGesture.allowedTouchTypes = on ? [NSNumber(value: UITouch.TouchType.pencil.rawValue)] : [NSNumber(value: UITouch.TouchType.direct.rawValue), NSNumber(value: UITouch.TouchType.pencil.rawValue)]
        pan?.minimumNumberOfTouches = on ? 1 : 2
    }

    /// fingers (and a trackpad's pointer) pan and pinch; the Pencil never does
    private static let navigationTouches = [NSNumber(value: UITouch.TouchType.direct.rawValue), NSNumber(value: UITouch.TouchType.indirectPointer.rawValue)]

    private func autoScroll(_ p: CGPoint) { // (a drag at an edge carries the course along)
        let edge: CGFloat = 36
        if p.x > bounds.width - edge { originX += 6 / scale; clampOrigin(); report(); setNeedsDisplay() }
        if p.x < edge { originX -= 6 / scale; clampOrigin(); report(); setNeedsDisplay() }
    }

    @objc private func panned(_ g: UIPanGestureRecognizer) {
        let d = g.translation(in: self)
        g.setTranslation(.zero, in: self)
        originX -= Double(d.x) / scale
        originY += Double(d.y) / scale
        clampOrigin()
        report()
        setNeedsDisplay()
    }

    @objc private func pinched(_ g: UIPinchGestureRecognizer) {
        zoom(by: Double(g.scale), around: g.location(in: self))
        g.scale = 1
    }

    @objc private func hovered(_ g: UIHoverGestureRecognizer) {
        woke()
        switch g.state {
        case .began, .changed:
            let w = world(g.location(in: self))
            hoverWorld = CGPoint(x: w.x, y: w.y)
        default: hoverWorld = nil
        }
    }

    @objc private func tapped(_ g: UITapGestureRecognizer) {
        guard let s = scene else { return }
        let w = world(g.location(in: self))
        if let n = s.notes.min(by: { abs($0.x - w.x) < abs($1.x - w.x) }), abs(n.x - w.x) < 2 { onTapNote?(n) }
    }

    // all at once: a pan and a pinch together; and the tool beside them, so a second finger landing after the tool
    // started still gets its pinch (the tool cancels itself when it lands: ToolGestureRecognizer)
    func gestureRecognizer(_ g: UIGestureRecognizer, shouldRecognizeSimultaneouslyWith o: UIGestureRecognizer) -> Bool { true }

    // the Pencil's own gestures: a double-tap (as the Pencil's settings say: the eraser, or the tool before), and the
    // Pencil Pro's squeeze (a palette of the tools at its tip, where it hovers)
    func pencilInteractionDidTap(_ interaction: UIPencilInteraction) { // (before iOS 17.5)
        onPencilTap?(UIPencilInteraction.preferredTapAction)
    }

    @available(iOS 17.5, *)
    func pencilInteraction(_ interaction: UIPencilInteraction, didReceiveTap tap: UIPencilInteraction.Tap) {
        onPencilTap?(UIPencilInteraction.preferredTapAction)
    }

    @available(iOS 17.5, *)
    func pencilInteraction(_ interaction: UIPencilInteraction, didReceiveSqueeze squeeze: UIPencilInteraction.Squeeze) {
        guard squeeze.phase == .ended else { return }
        let at = squeeze.hoverPose?.location ?? CGPoint(x: bounds.midX, y: bounds.midY)
        if UIPencilInteraction.preferredSqueezeAction != .ignore { onPencilSqueeze?(at) }
    }

    // ⌥ held (a hardware keyboard): the tool erases
    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        if presses.contains(where: { $0.key?.keyCode == .keyboardLeftAlt || $0.key?.keyCode == .keyboardRightAlt }) { onErase?(true) }
        super.pressesBegan(presses, with: event)
    }
    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        if presses.contains(where: { $0.key?.keyCode == .keyboardLeftAlt || $0.key?.keyCode == .keyboardRightAlt }) { onErase?(false) }
        super.pressesEnded(presses, with: event)
    }

    // MARK: drawing
    /// the course: redrawn when it or the view changes
    private func drawCourse(_ ctx: CGContext) {
        guard let s = scene else { return }
        let look = ZoneLook.of(s.zone)
        let cell = s.cell
        let c0 = max(0, Int(floor(originX / cell)) - 1), c1 = Int(ceil((originX + Double(bounds.width) / scale) / cell)) + 1
        drawGround(ctx, look)
        if showGrid && cell * scale > 7 { drawGrid(ctx, s, c0, c1, look) }
        for w in s.walls where w.c1 >= c0 && w.c0 <= c1 { drawWall(ctx, w, cell, look) }
        for g in s.gauntlets where g.c1 >= c0 && g.c0 <= c1 { drawGauntlet(ctx, g, cell, look) }
        drawBands(ctx)
        drawFinish(ctx, s.length)
        if showBot, let run = botRun { drawRun(ctx, run.trace.map { CGPoint(x: $0[0], y: $0[1] + $0[2] * cell) }, UIColor(hex: "#29e7ff", alpha: 0.85), dashed: false)
            for e in run.events where e.count >= 3 { dot(ctx, screen(e[0], e[1]), 3.5, e[2] == 0 ? UIColor(hex: "#ff2bd6") : UIColor(hex: "#29e7ff")) } }
        if !playTrace.isEmpty { drawRun(ctx, playTrace, UIColor(hex: "#3dff8a", alpha: 0.9), dashed: true) }
        if showWalk { drawWalk(ctx, s) }
        drawRuler(ctx, look)
    }

    /// what moves on it: redrawn every frame while any of it is in view
    private func drawMotion(_ ctx: CGContext) {
        guard let s = scene else { return }
        let look = ZoneLook.of(s.zone)
        let cell = s.cell
        if let sel = s.selection, sel.count == 2 { drawBand(ctx, Double(sel[0]) * cell, Double(sel[1] + 1) * cell, UIColor(hex: "#29e7ff", alpha: 0.16), UIColor(hex: "#29e7ff", alpha: 0.8)) }
        if let p = s.preview { drawPreview(ctx, p, cell, look) }
        for it in s.items where it.x >= originX - 2 && it.x <= originX + Double(bounds.width) / scale + 2 { drawItem(ctx, it, picked: it.i == s.picked) }
        if showNotes { for n in s.notes where n.x >= originX - 2 && n.x <= originX + Double(bounds.width) / scale + 2 { drawNote(ctx, n) } }
        if let st = s.start { drawStart(ctx, st) }
        if let h = hoverWorld { drawHover(ctx, h, cell, look) }
    }

    private func drawGround(_ ctx: CGContext, _ look: ZoneLook) {
        let y = screen(0, 0).y
        ctx.setFillColor(UIColor(look.ground).cgColor)
        ctx.fill(CGRect(x: 0, y: y, width: bounds.width, height: bounds.height - y))
        ctx.setStrokeColor(look.uiAccent.withAlphaComponent(0.2).cgColor) // (its glow: a wide faint line, no blur)
        ctx.setLineWidth(6)
        ctx.strokeLineSegments(between: [CGPoint(x: 0, y: y), CGPoint(x: bounds.width, y: y)])
        ctx.setStrokeColor(look.uiAccent.withAlphaComponent(0.8).cgColor)
        ctx.setLineWidth(1.5)
        ctx.strokeLineSegments(between: [CGPoint(x: 0, y: y), CGPoint(x: bounds.width, y: y)])
    }

    private func drawGrid(_ ctx: CGContext, _ s: CourseScene, _ c0: Int, _ c1: Int, _ look: ZoneLook) {
        ctx.setStrokeColor(look.uiAccent.withAlphaComponent(0.07).cgColor)
        ctx.setLineWidth(0.5)
        var pts: [CGPoint] = []
        let top = screen(0, Double(s.rows + 6) * s.cell).y
        for c in c0...c1 { let x = screen(Double(c) * s.cell, 0).x; pts += [CGPoint(x: x, y: top), CGPoint(x: x, y: screen(0, 0).y)] }
        for r in 0...(s.rows + 6) { let y = screen(0, Double(r) * s.cell).y; pts += [CGPoint(x: 0, y: y), CGPoint(x: bounds.width, y: y)] }
        ctx.strokeLineSegments(between: pts)
    }

    private func rect(_ x0: Double, _ y0: Double, _ x1: Double, _ y1: Double) -> CGRect {
        let a = screen(x0, y1), b = screen(x1, y0)
        return CGRect(x: a.x, y: a.y, width: b.x - a.x, height: b.y - a.y)
    }

    private func fillCells(_ ctx: CGContext, _ r: CGRect, kind: String, look: ZoneLook, topHay: Bool = false, cell: Double) {
        let hay = kind == "hay"
        ctx.setFillColor(UIColor(hay ? Neon.hay : look.crateLit).cgColor)
        ctx.fill(r)
        let z = CGFloat(cell * scale)
        if z > 9 { // (each crate's or bale's face)
            ctx.setStrokeColor((hay ? UIColor(hex: "#ffc23a", alpha: 0.35) : UIColor(look.crateEdge).withAlphaComponent(0.35)).cgColor)
            ctx.setLineWidth(1)
            var y = r.maxY
            while y > r.minY + 0.5 {
                var x = r.minX
                while x < r.maxX - 0.5 {
                    let face = CGRect(x: x + 1.5, y: y - z + 1.5, width: z - 3, height: z - 3)
                    ctx.stroke(face)
                    if hay { ctx.strokeLineSegments(between: [CGPoint(x: face.minX, y: face.midY), CGPoint(x: face.maxX, y: face.midY)]) }
                    else { ctx.strokeLineSegments(between: [CGPoint(x: face.minX, y: face.minY), CGPoint(x: face.maxX, y: face.maxY)]) }
                    x += z
                }
                y -= z
            }
        }
        if topHay {
            let top = CGRect(x: r.minX, y: r.minY, width: r.width, height: min(r.height, z))
            ctx.setFillColor(UIColor(Neon.hayLit).cgColor)
            ctx.fill(top)
        }
        let edge = hay ? UIColor(hex: "#ffc23a") : look.uiAccent // (its glow: a wide faint line under a bright one, no blur)
        ctx.setStrokeColor(edge.withAlphaComponent(0.22).cgColor)
        ctx.setLineWidth(5)
        ctx.strokeLineSegments(between: [CGPoint(x: r.minX, y: r.minY), CGPoint(x: r.maxX, y: r.minY)])
        ctx.setStrokeColor(edge.withAlphaComponent(0.85).cgColor)
        ctx.setLineWidth(1.4)
        ctx.strokeLineSegments(between: [CGPoint(x: r.minX, y: r.minY), CGPoint(x: r.maxX, y: r.minY)])
    }

    /// a bumper or a gate wall, cell by cell as the maze draws them: a bumper deep violet with its pink ring and
    /// chevrons pointing back the way it sends her (pulsing); a gate a green field with its arrow running its way
    private func drawBouncer(_ ctx: CGContext, _ w: WallRun, _ cell: Double) {
        let pink = UIColor(hex: "#ff2bd6"), green = UIColor(hex: "#2bff9a")
        for c in w.c0..<w.c1 {
            for r in 0..<w.h {
                let f = rect(Double(c) * cell, Double(r) * cell, Double(c + 1) * cell, Double(r + 1) * cell)
                if w.kind == "bumper" {
                    ctx.setFillColor(UIColor(hex: "#160a20").cgColor); ctx.fill(f)
                    let beat = CGFloat(0.6 + 0.4 * sin(time * 6 + Double(r) * 0.8))
                    ctx.setShadow(offset: .zero, blur: 8 * beat, color: pink.cgColor)
                    ctx.setStrokeColor(pink.withAlphaComponent(0.6 + 0.4 * beat).cgColor); ctx.setLineWidth(max(1, f.width * 0.07))
                    ctx.strokeEllipse(in: f.insetBy(dx: f.width * 0.16, dy: f.height * 0.16))
                    ctx.setShadow(offset: .zero, blur: 0, color: nil)
                    if f.width > 10 { // its chevron: back, the way it sends her
                        ctx.setStrokeColor(UIColor.white.withAlphaComponent(0.5 + 0.4 * beat).cgColor); ctx.setLineWidth(1.4)
                        let m = CGPoint(x: f.midX, y: f.midY), a = f.width * 0.14
                        ctx.strokeLineSegments(between: [CGPoint(x: m.x + a, y: m.y - a), CGPoint(x: m.x - a * 0.4, y: m.y), CGPoint(x: m.x - a * 0.4, y: m.y), CGPoint(x: m.x + a, y: m.y + a)])
                    }
                } else {
                    ctx.setFillColor(green.withAlphaComponent(0.16).cgColor); ctx.fill(f)
                    ctx.setStrokeColor(green.withAlphaComponent(0.45).cgColor); ctx.setLineWidth(1); ctx.stroke(f.insetBy(dx: 0.5, dy: 0.5))
                    let d: CGFloat = w.kind == "gate>" ? 1 : -1
                    let flow = CGFloat((time * 1.6 + Double(r) * 0.3).truncatingRemainder(dividingBy: 1))
                    let cx = f.midX + d * (flow - 0.5) * f.width * 0.5, a = f.width * 0.16
                    ctx.setStrokeColor(green.withAlphaComponent(1 - abs(flow - 0.5)).cgColor); ctx.setLineWidth(max(1.2, f.width * 0.06))
                    ctx.strokeLineSegments(between: [CGPoint(x: cx - d * a, y: f.midY - a), CGPoint(x: cx + d * a * 0.4, y: f.midY), CGPoint(x: cx + d * a * 0.4, y: f.midY), CGPoint(x: cx - d * a, y: f.midY + a)])
                }
            }
        }
    }

    private func drawWall(_ ctx: CGContext, _ w: WallRun, _ cell: Double, _ look: ZoneLook) {
        let x0 = Double(w.c0) * cell, x1 = Double(w.c1) * cell
        if w.kind == "bumper" || w.kind == "gate>" || w.kind == "gate<" { drawBouncer(ctx, w, cell); return }
        if w.h > 0 { fillCells(ctx, rect(x0, 0, x1, Double(w.h) * cell), kind: w.kind, look: look, topHay: w.kind == "mix", cell: cell) }
        if w.top > 0 { // a floating block
            fillCells(ctx, rect(x0, Double(w.ceil) * cell, x1, Double(w.top) * cell), kind: w.kind, look: look, topHay: w.kind == "mix", cell: cell)
            underside(ctx, x0, x1, Double(w.ceil) * cell, UIColor(hex: "#ff2bd6"))
        } else if w.ceil > 0 { // a barrier: down from the sky to its underside
            let r = rect(x0, Double(w.ceil) * cell, x1, Double(w.ceil) * cell + 1.6)
            ctx.saveGState()
            ctx.clip(to: r)
            ctx.setFillColor(UIColor(hex: "#2a0d2e", alpha: 0.85).cgColor)
            ctx.fill(r)
            ctx.setStrokeColor(UIColor(hex: "#ff2bd6", alpha: 0.35).cgColor)
            ctx.setLineWidth(2)
            var x = r.minX - r.height
            while x < r.maxX { ctx.strokeLineSegments(between: [CGPoint(x: x, y: r.maxY), CGPoint(x: x + r.height, y: r.minY)]); x += 9 }
            ctx.restoreGState()
            underside(ctx, x0, x1, Double(w.ceil) * cell, UIColor(hex: "#ff2bd6"))
        }
    }

    private func underside(_ ctx: CGContext, _ x0: Double, _ x1: Double, _ y: Double, _ color: UIColor) {
        let a = screen(x0, y), b = screen(x1, y)
        ctx.setStrokeColor(color.cgColor)
        ctx.setShadow(offset: .zero, blur: 10, color: color.cgColor)
        ctx.setLineWidth(2)
        ctx.strokeLineSegments(between: [a, b])
        ctx.setShadow(offset: .zero, blur: 0, color: nil)
    }

    private func drawGauntlet(_ ctx: CGContext, _ g: GauntletSpan, _ cell: Double, _ look: ZoneLook) {
        let rows = g.rows ?? []
        let r = rect(Double(g.c0) * cell, 0, Double(g.c1) * cell, Double(max(rows.count, 4)) * cell)
        ctx.setFillColor(UIColor(hex: "#0b0716", alpha: 0.85).cgColor)
        ctx.fill(r)
        let firstCol = max(0, Int((originX - Double(g.c0) * cell) / cell) - 1)
        let lastCol = min(g.c1 - g.c0 - 1, Int((originX + Double(bounds.width) / scale - Double(g.c0) * cell) / cell) + 1)
        if firstCol <= lastCol {
            for (i, row) in rows.enumerated() {
                let chars = Array(row)
                let y = Double(rows.count - 1 - i) * cell
                for c in firstCol...lastCol where c < chars.count {
                    let ch = chars[c]
                    if ch == "." { continue }
                    MazePaint.cell(ctx, ch, rect(Double(g.c0 + c) * cell, y, Double(g.c0 + c + 1) * cell, y + cell).insetBy(dx: 0.5, dy: 0.5), look, time: time) // (as the maze editor draws them)
                }
            }
            MazePaint.beams(ctx, rows, cols: firstCol...lastCol) { c, i in
                let y = Double(rows.count - 1 - i) * cell
                return rect(Double(g.c0 + c) * cell, y, Double(g.c0 + c + 1) * cell, y + cell)
            }
        }
        ctx.setStrokeColor(UIColor(hex: "#ffc23a", alpha: 0.9).cgColor)
        ctx.setShadow(offset: .zero, blur: 10, color: UIColor(hex: "#ffc23a").cgColor)
        ctx.setLineWidth(1.5)
        ctx.stroke(r)
        ctx.setShadow(offset: .zero, blur: 0, color: nil)
        label(ctx, "THE " + g.name.uppercased(), at: CGPoint(x: max(r.minX + 6, 6), y: r.minY - 16), color: UIColor(hex: "#ffc23a"))
    }

    private func blade(_ ctx: CGContext, _ hub: CGPoint, _ r: CGFloat, _ color: UIColor, spin: Double = 0) {
        MazePaint.blade(ctx, hub, r, color, spin: spin)
    }

    private func drawItem(_ ctx: CGContext, _ it: PlacedItem, picked: Bool) {
        let type = registry.hazard(it.type)
        let color = UIColor(hex: type?.color ?? "#ffffff")
        let p = screen(it.x, it.y)
        switch type?.glyph ?? "marker" {
        case "saw":
            let cube = (scene?.walls.first { Double($0.c0) * (scene?.cell ?? 0.6) <= it.x && it.x < Double($0.c1) * (scene?.cell ?? 0.6) }).map { $0.h > 0 && $0.kind != "crate" } ?? false
            blade(ctx, p, CGFloat(0.5 * scale), cube ? UIColor(hex: "#29e7ff") : color, spin: time * 6)
        case "disco":
            let r = CGFloat(0.28 * scale)
            let tw = 0.6 + 0.4 * sin(time * 3 + it.x)
            ctx.setShadow(offset: .zero, blur: 14 * CGFloat(tw), color: color.cgColor)
            let colors = [UIColor.white.cgColor, color.cgColor, UIColor(hex: "#ff2bd6").cgColor] as CFArray
            if let g = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: colors, locations: [0, 0.45, 1]) {
                ctx.saveGState()
                ctx.addEllipse(in: CGRect(x: p.x - r, y: p.y - r, width: 2 * r, height: 2 * r)); ctx.clip()
                ctx.drawRadialGradient(g, startCenter: CGPoint(x: p.x - r * 0.35, y: p.y - r * 0.35), startRadius: 0, endCenter: p, endRadius: r * 1.2, options: [])
                ctx.setStrokeColor(UIColor(white: 1, alpha: 0.35).cgColor); ctx.setLineWidth(0.8)
                ctx.strokeLineSegments(between: [CGPoint(x: p.x - r, y: p.y), CGPoint(x: p.x + r, y: p.y), CGPoint(x: p.x, y: p.y - r), CGPoint(x: p.x, y: p.y + r)])
                ctx.restoreGState()
            }
            ctx.setShadow(offset: .zero, blur: 0, color: nil)
        case "laser": // a wall laser: its turret on the wall's side, the pointer's dashed line along its reach, a dot where it ends
            let d: CGFloat = (it.v.count > 2 ? it.v[2] : 0) > 0.5 ? 1 : -1
            let hw = CGFloat(0.16 * scale), hh = CGFloat(0.2 * scale)
            ctx.setFillColor(UIColor(hex: "#1b1826").cgColor)
            ctx.fill(CGRect(x: d > 0 ? p.x : p.x - hw, y: p.y - hh, width: hw, height: 2 * hh))
            ctx.setStrokeColor(color.withAlphaComponent(0.7).cgColor); ctx.setLineWidth(1)
            ctx.stroke(CGRect(x: d > 0 ? p.x : p.x - hw, y: p.y - hh, width: hw, height: 2 * hh))
            let tip = CGPoint(x: p.x + d * CGFloat(0.37 * scale), y: p.y)
            ctx.setStrokeColor(UIColor(hex: "#2b2838").cgColor); ctx.setLineWidth(max(2, CGFloat(0.17 * scale)))
            ctx.strokeLineSegments(between: [CGPoint(x: p.x + d * hw, y: p.y), tip])
            let charge = CGFloat(0.5 + 0.5 * sin(time * 5 + it.x)) // (charging, again and again: its warning)
            if let reach = it.reach, reach.count == 2 {
                let end = screen(d > 0 ? reach[1] : reach[0], it.y)
                ctx.setStrokeColor(color.withAlphaComponent(0.35 + 0.5 * charge).cgColor); ctx.setLineWidth(1.2)
                ctx.setLineDash(phase: CGFloat(-time * 30) * d, lengths: [6, 4])
                ctx.strokeLineSegments(between: [tip, CGPoint(x: end.x, y: tip.y)])
                ctx.setLineDash(phase: 0, lengths: [])
                ctx.setShadow(offset: .zero, blur: 10 * charge, color: color.cgColor)
                ctx.setFillColor(color.cgColor)
                ctx.fillEllipse(in: CGRect(x: end.x - 3, y: tip.y - 3, width: 6, height: 6))
            }
            let r = CGFloat(max(3, 0.08 * scale))
            ctx.setShadow(offset: .zero, blur: 6 + 10 * charge, color: color.cgColor)
            ctx.setFillColor(color.cgColor)
            ctx.fillEllipse(in: CGRect(x: tip.x - r, y: tip.y - r, width: 2 * r, height: 2 * r))
            ctx.setShadow(offset: .zero, blur: 0, color: nil)
            if scale > 12 { label(ctx, "\(Int(it.v.count > 1 ? it.v[1] : 0))", at: CGPoint(x: d > 0 ? p.x - 12 : p.x + 4, y: p.y - hh - 13), color: color, size: 10) }
        case "lava": // a stretch of the surface molten: glowing, churning, bubbling
            let x1 = it.x1 ?? it.x + (it.v.count > 1 ? it.v[1] : 1) * (scene?.cell ?? 0.6)
            let a = screen(it.x, it.y), b = screen(x1, it.y + 0.14)
            let f = CGRect(x: a.x, y: b.y, width: b.x - a.x, height: a.y - b.y)
            ctx.setShadow(offset: .zero, blur: 16, color: color.cgColor)
            let colors = [UIColor(hex: "#ffc23a").cgColor, color.cgColor, UIColor(hex: "#b3200c").cgColor] as CFArray
            if let g = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: colors, locations: [0, 0.45, 1]) {
                ctx.saveGState(); ctx.clip(to: f)
                let shift = CGFloat(sin(time * 1.7 + it.x)) * f.width * 0.15
                ctx.drawLinearGradient(g, start: CGPoint(x: f.minX + shift, y: f.minY), end: CGPoint(x: f.maxX + shift, y: f.maxY), options: [.drawsBeforeStartLocation, .drawsAfterEndLocation])
                ctx.restoreGState()
            }
            ctx.setShadow(offset: .zero, blur: 0, color: nil)
            ctx.setFillColor(UIColor(hex: "#fff1c8", alpha: 0.85).cgColor) // (bubbles)
            for k in 0..<max(1, Int(f.width / 14)) {
                let ph = time * 2.2 + Double(k) * 1.7 + it.x
                let bx = f.minX + CGFloat((Double(k) + 0.5) / Double(max(1, Int(f.width / 14)))) * f.width
                let rr = CGFloat(1.5 + 2 * max(0, sin(ph)))
                ctx.fillEllipse(in: CGRect(x: bx - rr, y: f.minY - rr * 0.6, width: 2 * rr, height: 2 * rr))
            }
        case "spring": // a spring: its coiled pad on the surface, its arc dotted where it flies her (red where it crashes)
            let w = CGFloat(0.55 * scale), base = CGFloat(0.06 * scale), coilH = CGFloat(0.22 * scale)
            let fire = CGFloat(max(0, sin(time * 3 + it.x))) // (its plate bobbing: it works)
            if let arc = it.arc, arc.count > 1 {
                let crash = (it.end ?? "landed") != "landed"
                let path = CGMutablePath()
                for (k, pt) in arc.enumerated() where pt.count == 2 {
                    let s = screen(pt[0], pt[1] + 0.3)
                    k == 0 ? path.move(to: s) : path.addLine(to: s)
                }
                ctx.setStrokeColor((crash ? UIColor(hex: "#ff2b4a") : color).withAlphaComponent(0.85).cgColor)
                ctx.setLineWidth(2); ctx.setLineCap(.round)
                ctx.setLineDash(phase: CGFloat(-time * 24), lengths: [1, 7])
                ctx.addPath(path); ctx.strokePath()
                ctx.setLineDash(phase: 0, lengths: []); ctx.setLineCap(.butt)
                if let last = arc.last, last.count == 2 {
                    let e = screen(last[0], last[1] + 0.3)
                    if crash { // where it crashes: a red cross
                        ctx.setStrokeColor(UIColor(hex: "#ff2b4a").cgColor); ctx.setLineWidth(2.5)
                        ctx.strokeLineSegments(between: [CGPoint(x: e.x - 6, y: e.y - 6), CGPoint(x: e.x + 6, y: e.y + 6), CGPoint(x: e.x - 6, y: e.y + 6), CGPoint(x: e.x + 6, y: e.y - 6)])
                    } else { // where she lands: a ring on the surface
                        let ring = CGFloat(5 + 2 * sin(time * 4))
                        ctx.setStrokeColor(color.cgColor); ctx.setLineWidth(1.5)
                        ctx.strokeEllipse(in: CGRect(x: e.x - ring, y: screen(last[0], last[1]).y - ring * 0.4, width: 2 * ring, height: ring * 0.8))
                    }
                }
            }
            MazePaint.springPad(ctx, p, scale: CGFloat(scale), fire: fire, color: color)
            let top = p.y - base - coilH * (1 - 0.25 * fire)
            if scale > 12 { label(ctx, "\(Int(it.v.count > 1 ? it.v[1] : 6))", at: CGPoint(x: p.x + w * 0.6, y: top - 12), color: color, size: 10) }
        default: // a hazard the editor has no drawing of: a diamond in its colour, its initial on it
            let r = CGFloat(max(7, 0.3 * scale))
            let d = CGMutablePath()
            d.move(to: CGPoint(x: p.x, y: p.y - r)); d.addLine(to: CGPoint(x: p.x + r, y: p.y)); d.addLine(to: CGPoint(x: p.x, y: p.y + r)); d.addLine(to: CGPoint(x: p.x - r, y: p.y)); d.closeSubpath()
            ctx.setShadow(offset: .zero, blur: 10, color: color.cgColor)
            ctx.setFillColor(color.withAlphaComponent(0.85).cgColor); ctx.addPath(d); ctx.fillPath()
            ctx.setShadow(offset: .zero, blur: 0, color: nil)
            label(ctx, String((type?.label ?? it.type).prefix(1)), at: CGPoint(x: p.x - 3.5, y: p.y - 7), color: .black)
        }
        if picked {
            let r = CGFloat(0.75 * scale) + CGFloat(2 * sin(time * 5))
            ctx.setStrokeColor(UIColor.white.cgColor)
            ctx.setLineDash(phase: CGFloat(time * 20), lengths: [5, 4])
            ctx.setLineWidth(1.5)
            ctx.strokeEllipse(in: CGRect(x: p.x - r, y: p.y - r, width: 2 * r, height: 2 * r))
            ctx.setLineDash(phase: 0, lengths: [])
        }
    }

    private func drawNote(_ ctx: CGContext, _ n: CourseNote) {
        let color = n.kind == "note" ? UIColor(hex: "#ffb43a") : UIColor(hex: "#ff2b4a")
        let p = screen(n.x, n.y)
        let k = CGFloat(1 + 0.12 * sin(time * 4))
        let r: CGFloat = 9 * k
        let tri = CGMutablePath()
        tri.move(to: CGPoint(x: p.x, y: p.y - 22 - r)); tri.addLine(to: CGPoint(x: p.x + r, y: p.y - 22 + r * 0.8)); tri.addLine(to: CGPoint(x: p.x - r, y: p.y - 22 + r * 0.8)); tri.closeSubpath()
        ctx.setShadow(offset: .zero, blur: 10, color: color.cgColor)
        ctx.setFillColor(color.cgColor); ctx.addPath(tri); ctx.fillPath()
        ctx.setShadow(offset: .zero, blur: 0, color: nil)
        label(ctx, "!", at: CGPoint(x: p.x - 2, y: p.y - 28), color: .black)
        ctx.setStrokeColor(color.withAlphaComponent(0.5).cgColor); ctx.setLineWidth(1)
        ctx.strokeLineSegments(between: [CGPoint(x: p.x, y: p.y - 22 + r * 0.8), p])
    }

    private func drawFinish(_ ctx: CGContext, _ x: Double) {
        let top = screen(x, 12).y, bottom = screen(x, 0).y, sx = screen(x, 0).x
        let sq: CGFloat = 6
        var y = top, i = 0
        while y < bottom {
            ctx.setFillColor((i % 2 == 0 ? UIColor.white : UIColor.black).cgColor)
            ctx.fill(CGRect(x: sx - sq, y: y, width: sq, height: sq))
            ctx.setFillColor((i % 2 == 0 ? UIColor.black : UIColor.white).cgColor)
            ctx.fill(CGRect(x: sx, y: y, width: sq, height: sq))
            y += sq; i += 1
        }
        label(ctx, "FINISH", at: CGPoint(x: sx + 8, y: top), color: .white)
    }

    private func drawStart(_ ctx: CGContext, _ x: Double) {
        let base = screen(x, heightAt(x)), top = CGPoint(x: base.x, y: base.y - 34)
        ctx.setStrokeColor(UIColor.white.cgColor); ctx.setLineWidth(2)
        ctx.strokeLineSegments(between: [base, top])
        let wave = CGFloat(2 * sin(time * 5))
        let flag = CGMutablePath()
        flag.move(to: top); flag.addLine(to: CGPoint(x: top.x + 18, y: top.y + 5 + wave)); flag.addLine(to: CGPoint(x: top.x, y: top.y + 12)); flag.closeSubpath()
        ctx.setShadow(offset: .zero, blur: 8, color: UIColor(hex: "#3dff8a").cgColor)
        ctx.setFillColor(UIColor(hex: "#3dff8a").cgColor); ctx.addPath(flag); ctx.fillPath()
        ctx.setShadow(offset: .zero, blur: 0, color: nil)
    }

    private func heightAt(_ x: Double) -> Double {
        guard let s = scene else { return 0 }
        let c = Int(floor(x / s.cell))
        return Double(s.walls.first { $0.c0 <= c && c < $0.c1 }?.h ?? 0) * s.cell
    }

    private func drawBand(_ ctx: CGContext, _ x0: Double, _ x1: Double, _ fill: UIColor, _ edge: UIColor) {
        let a = screen(x0, 0).x, b = screen(x1, 0).x
        let r = CGRect(x: a, y: 0, width: b - a, height: streetY)
        ctx.setFillColor(fill.cgColor); ctx.fill(r)
        ctx.setStrokeColor(edge.cgColor); ctx.setLineWidth(1); ctx.setLineDash(phase: CGFloat(time * 16), lengths: [6, 4])
        ctx.stroke(r); ctx.setLineDash(phase: 0, lengths: [])
    }

    /// where a roll placed a chunk (a magenta outline and its name), or a chunk's pads (dim, hatched: stripped when it's placed)
    private func drawBands(_ ctx: CGContext) {
        let top = insets.top + 48
        for b in bands {
            let a = screen(b.x0, 0).x, z = screen(b.x1, 0).x
            guard z >= 0, a <= bounds.width else { continue }
            let color = UIColor(hex: b.hex)
            let r = CGRect(x: a, y: top, width: z - a, height: max(0, streetY - top))
            ctx.setFillColor(color.withAlphaComponent(b.stripped ? 0.09 : 0.1).cgColor); ctx.fill(r)
            if b.stripped { // (hatched)
                ctx.saveGState(); ctx.clip(to: r)
                ctx.setStrokeColor(color.withAlphaComponent(0.2).cgColor); ctx.setLineWidth(1)
                var x = r.minX - r.height
                while x < r.maxX { ctx.strokeLineSegments(between: [CGPoint(x: x, y: r.maxY), CGPoint(x: x + r.height, y: r.minY)]); x += 14 }
                ctx.restoreGState()
            }
            ctx.setStrokeColor(color.withAlphaComponent(b.stripped ? 0.5 : 0.85).cgColor); ctx.setLineWidth(b.stripped ? 1 : 1.5)
            ctx.setLineDash(phase: 0, lengths: b.stripped ? [3, 4] : [])
            ctx.stroke(r)
            ctx.setLineDash(phase: 0, lengths: [])
            label(ctx, b.label.uppercased(), at: CGPoint(x: max(a, 0) + 6, y: top + 4), color: color.withAlphaComponent(0.95), size: 10)
        }
    }

    private func drawPreview(_ ctx: CGContext, _ p: ToolPreview, _ cell: Double, _ look: ZoneLook) {
        let color = p.erase ? UIColor(hex: "#ff6a80") : (p.kind == "gauntlet" ? UIColor(hex: "#ffc23a") : UIColor(hex: "#f4ff5a"))
        switch p.kind {
        case "block":
            let r = rect(Double(p.c0) * cell, Double(p.r0) * cell, Double(p.c1 + 1) * cell, Double(max(p.r1, p.r0 + 1)) * cell)
            ctx.setFillColor(color.withAlphaComponent(0.18).cgColor); ctx.fill(r)
            ctx.setStrokeColor(color.cgColor); ctx.setLineWidth(1.5); ctx.setLineDash(phase: CGFloat(time * 16), lengths: [6, 4]); ctx.stroke(r); ctx.setLineDash(phase: 0, lengths: [])
        case "cols": drawBand(ctx, Double(p.c0) * cell, Double(p.c1 + 1) * cell, color.withAlphaComponent(0.15), color)
        case "gauntlet": drawBand(ctx, Double(p.c0) * cell, Double(p.c1) * cell, color.withAlphaComponent(0.12), color)
        default: break
        }
    }

    private func drawRun(_ ctx: CGContext, _ pts: [CGPoint], _ color: UIColor, dashed: Bool) {
        guard pts.count > 1 else { return }
        let path = CGMutablePath()
        path.addLines(between: pts.map { screen($0.x, $0.y) })
        ctx.setStrokeColor(color.cgColor)
        ctx.setLineWidth(2)
        ctx.setShadow(offset: .zero, blur: 6, color: color.cgColor)
        if dashed { ctx.setLineDash(phase: 0, lengths: [6, 4]) }
        ctx.addPath(path); ctx.strokePath()
        ctx.setLineDash(phase: 0, lengths: [])
        ctx.setShadow(offset: .zero, blur: 0, color: nil)
    }

    private func dot(_ ctx: CGContext, _ p: CGPoint, _ r: CGFloat, _ c: UIColor) {
        ctx.setShadow(offset: .zero, blur: 6, color: c.cgColor)
        ctx.setFillColor(c.cgColor); ctx.fillEllipse(in: CGRect(x: p.x - r, y: p.y - r, width: 2 * r, height: 2 * r))
        ctx.setShadow(offset: .zero, blur: 0, color: nil)
    }

    /// the meter a careful player holds along the course (8 eggs at its top), a strip across the top
    private func drawWalk(_ ctx: CGContext, _ s: CourseScene) {
        guard s.walk.count > 1 else { return }
        let band = CGRect(x: 0, y: insets.top + 8, width: bounds.width, height: 34)
        ctx.setFillColor(UIColor(white: 0, alpha: 0.28).cgColor)
        ctx.fill(band)
        let path = CGMutablePath()
        var first = true
        for w in s.walk where w.count == 2 {
            let x = CGFloat((w[0] - originX) * scale), y = band.maxY - CGFloat(w[1] / 8) * band.height
            if first { path.move(to: CGPoint(x: x, y: y)); first = false } else { path.addLine(to: CGPoint(x: x, y: y)) }
        }
        ctx.setStrokeColor(UIColor(Neon.egg).cgColor); ctx.setLineWidth(1.5)
        ctx.setShadow(offset: .zero, blur: 4, color: UIColor(Neon.egg).cgColor)
        ctx.addPath(path); ctx.strokePath()
        ctx.setShadow(offset: .zero, blur: 0, color: nil)
        let low = band.maxY - CGFloat(1.4 / 8) * band.height // (an egg and the margin: below it, short)
        ctx.setStrokeColor(UIColor(hex: "#ff2b4a", alpha: 0.5).cgColor); ctx.setLineDash(phase: 0, lengths: [3, 3]); ctx.setLineWidth(1)
        ctx.strokeLineSegments(between: [CGPoint(x: 0, y: low), CGPoint(x: bounds.width, y: low)])
        ctx.setLineDash(phase: 0, lengths: [])
        label(ctx, "EGG METER", at: CGPoint(x: 8, y: band.minY + 2), color: UIColor(Neon.egg).withAlphaComponent(0.6), size: 9)
    }

    private func drawHover(_ ctx: CGContext, _ h: CGPoint, _ cell: Double, _ look: ZoneLook) {
        if tool == "pan" || tool == "select" { return } // (nothing to land)
        let c = floor(h.x / cell), r = max(0, (h.y / cell).rounded())
        let color = look.uiAccent.withAlphaComponent(0.8)
        if let type = registry.hazard(tool) {
            let y = type.isFree || type.isFace ? h.y : heightAt(h.x)
            dot(ctx, screen(h.x, y), 6, UIColor(hex: type.color, alpha: 0.6))
        } else {
            let box = rect(c * cell, 0, (c + 1) * cell, r * cell)
            ctx.setStrokeColor(color.cgColor); ctx.setLineWidth(1.2); ctx.setLineDash(phase: 0, lengths: [4, 3]); ctx.stroke(box); ctx.setLineDash(phase: 0, lengths: [])
        }
    }

    private func drawRuler(_ ctx: CGContext, _ look: ZoneLook) {
        let y = streetY
        ctx.setFillColor(UIColor(white: 0, alpha: 0.5).cgColor)
        ctx.fill(CGRect(x: 0, y: y + 4, width: bounds.width, height: 26))
        let step: Double = scale > 20 ? 5 : scale > 8 ? 10 : 50
        var m = (originX / step).rounded(.down) * step
        while m < originX + Double(bounds.width) / scale {
            let x = CGFloat((m - originX) * scale)
            ctx.setStrokeColor(look.uiAccent.withAlphaComponent(0.6).cgColor); ctx.setLineWidth(1)
            ctx.strokeLineSegments(between: [CGPoint(x: x, y: y + 4), CGPoint(x: x, y: y + 12)])
            label(ctx, "\(Int(m)) m", at: CGPoint(x: x + 3, y: y + 13), color: UIColor(white: 1, alpha: 0.7), size: 10)
            m += step
        }
    }

    private func label(_ ctx: CGContext, _ s: String, at p: CGPoint, color: UIColor, size: CGFloat = 11) {
        (s as NSString).draw(at: p, withAttributes: [.font: UIFont.systemFont(ofSize: size, weight: .bold), .foregroundColor: color])
    }
}

/// the canvas in SwiftUI
/// the canvas with the Pencil Pro's palette over it (a squeeze)
struct CourseCanvasArea: View {
    @ObservedObject var model: EditorModel
    var insets: UIEdgeInsets = .zero
    var body: some View {
        CourseCanvas(model: model, insets: insets)
            .overlay { if let p = model.palette { PencilPalette(model: model, at: p) } }
    }
}

struct CourseCanvas: UIViewRepresentable {
    @ObservedObject var model: EditorModel
    var insets: UIEdgeInsets = .zero

    func makeUIView(context: Context) -> CourseCanvasView {
        let v = CourseCanvasView(frame: .zero)
        model.canvas = v
        v.onPointer = { [weak model] phase, x, y in model?.pointer(phase, x: x, y: y) }
        v.onViewport = { [weak model] a, b in model?.viewport.from = a; model?.viewport.to = b }
        v.onErase = { [weak model] on in model?.erase = on }
        v.onTapNote = { [weak model] n in model?.focusedNote = n }
        v.onPencilTap = { [weak model] action in model?.pencilTap(action) }
        v.onPencilSqueeze = { [weak model] p in withAnimation(.spring(response: 0.35, dampingFraction: 0.75)) { model?.palette = p } }
        return v
    }

    func updateUIView(_ v: CourseCanvasView, context: Context) {
        v.insets = insets
        v.registry = model.registry
        v.tool = model.tool
        v.scene = model.scene
        v.botRun = model.botReport?.game
        v.playTrace = model.playTrace
        v.showWalk = model.showWalk
        v.showBot = model.showBot
        v.showNotes = model.showNotes
        v.bands = model.bands
    }
}
