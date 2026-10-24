// A maze's grid, drawn cell by cell in the zone's light (the course canvas draws its gauntlets with the same cells:
// MazePaint), with what the checker says over it: the bot's way through (cyan running right, pink running left, a dot
// where she lays), where she gets (green: on the way through, red: a dead end), the near misses, where the meter ran
// dry or the game's bot crashed; the tool's preview, the play-test's start, rulers along the top and the left. One
// finger (or the Pencil) uses the tool; two pan and pinch. Still unless something changes (but for a spring's bobbing plate).
import SwiftUI
import UIKit

/// a maze's cells, drawn the same in the maze editor and on the course
enum MazePaint {
    static func cell(_ ctx: CGContext, _ ch: Character, _ f: CGRect, _ look: ZoneLook, time: Double = 0) {
        switch ch {
        case "#":
            ctx.setFillColor(UIColor(look.crate).cgColor); ctx.fill(f)
            if f.width > 8 { ctx.setStrokeColor(UIColor(look.crateEdge).withAlphaComponent(0.25).cgColor); ctx.setLineWidth(1); ctx.stroke(f.insetBy(dx: 1, dy: 1)) }
        case "H":
            ctx.setFillColor(UIColor(hex: "#123a52").cgColor); ctx.fill(f)
            ctx.setStrokeColor(UIColor(hex: "#29e7ff", alpha: 0.6).cgColor); ctx.setLineWidth(1); ctx.stroke(f.insetBy(dx: 1, dy: 1))
        case "B":
            ctx.setStrokeColor(UIColor(hex: "#ff2bd6").cgColor); ctx.setLineWidth(1.5)
            ctx.strokeEllipse(in: f.insetBy(dx: f.width * 0.15, dy: f.height * 0.15))
        case "R", "L":
            ctx.setFillColor(UIColor(hex: "#3dff8a", alpha: 0.85).cgColor)
            let p = CGMutablePath()
            if ch == "R" { p.move(to: CGPoint(x: f.minX + f.width * 0.25, y: f.minY + 2)); p.addLine(to: CGPoint(x: f.maxX - 2, y: f.midY)); p.addLine(to: CGPoint(x: f.minX + f.width * 0.25, y: f.maxY - 2)) }
            else { p.move(to: CGPoint(x: f.maxX - f.width * 0.25, y: f.minY + 2)); p.addLine(to: CGPoint(x: f.minX + 2, y: f.midY)); p.addLine(to: CGPoint(x: f.maxX - f.width * 0.25, y: f.maxY - 2)) }
            ctx.addPath(p); ctx.fillPath()
        case "S", "C":
            if ch == "C" { ctx.setFillColor(UIColor(hex: "#123a52").cgColor) } else { ctx.setFillColor(UIColor(look.crate).cgColor) }
            ctx.fill(f)
            blade(ctx, CGPoint(x: f.midX, y: f.minY), f.width * 0.45, ch == "C" ? UIColor(hex: "#29e7ff") : UIColor(hex: "#ff3d6a"))
        case "(", ")": // stone with a wall laser in its side: the turret on that face, its lens glowing
            ctx.setFillColor(UIColor(look.crate).cgColor); ctx.fill(f)
            let d: CGFloat = ch == ")" ? 1 : -1
            let face = d > 0 ? f.maxX : f.minX
            let hw = max(2, f.width * 0.22), hh = max(3, f.height * 0.32)
            ctx.setFillColor(UIColor(hex: "#1b1826").cgColor)
            ctx.fill(CGRect(x: d > 0 ? face - hw : face, y: f.midY - hh, width: hw, height: 2 * hh))
            let tip = CGPoint(x: face + d * max(1.5, f.width * 0.12), y: f.midY)
            ctx.setStrokeColor(UIColor(hex: "#2b2838").cgColor); ctx.setLineWidth(max(1.5, f.height * 0.14))
            ctx.strokeLineSegments(between: [CGPoint(x: face, y: f.midY), tip])
            ctx.setShadow(offset: .zero, blur: 8, color: UIColor(hex: "#ff2b4a").cgColor)
            ctx.setFillColor(UIColor(hex: "#ff2b4a").cgColor)
            let lr = max(1.5, f.height * 0.1)
            ctx.fillEllipse(in: CGRect(x: tip.x - lr, y: tip.y - lr, width: 2 * lr, height: 2 * lr))
            ctx.setShadow(offset: .zero, blur: 0, color: nil)
        case "~": // lava: basalt under a molten top, glowing
            ctx.setFillColor(UIColor(hex: "#2a140c").cgColor); ctx.fill(f)
            let top = CGRect(x: f.minX, y: f.minY, width: f.width, height: max(2, f.height * 0.35))
            ctx.setShadow(offset: .zero, blur: 10, color: UIColor(hex: "#ff5a14").cgColor)
            ctx.setFillColor(UIColor(hex: "#ff6a1a").cgColor); ctx.fill(top)
            ctx.setFillColor(UIColor(hex: "#ffc23a").cgColor); ctx.fill(top.insetBy(dx: f.width * 0.25, dy: top.height * 0.3))
            ctx.setShadow(offset: .zero, blur: 0, color: nil)
        case "^": // a spring: stone under the course spring's pad (its plate bobbing with `time`; still at 0)
            ctx.setFillColor(UIColor(look.crate).cgColor); ctx.fill(f)
            if f.width > 8 { ctx.setStrokeColor(UIColor(look.crateEdge).withAlphaComponent(0.25).cgColor); ctx.setLineWidth(1); ctx.stroke(f.insetBy(dx: 1, dy: 1)) }
            ctx.setFillColor(UIColor(hex: "#7dff4a", alpha: 0.9).cgColor)
            ctx.fill(CGRect(x: f.minX + f.width * 0.15, y: f.minY, width: f.width * 0.7, height: max(1.5, f.height * 0.08)))
            springPad(ctx, CGPoint(x: f.midX, y: f.minY), scale: f.width / 0.6, fire: CGFloat(max(0, sin(time * 3 + Double(f.minX) * 0.05))), color: UIColor(hex: "#7dff4a"))
        case ">": ctx.setFillColor(UIColor(hex: "#3dff8a", alpha: 0.35).cgColor); ctx.fill(f)
        case "X": ctx.setFillColor(UIColor(hex: "#ffc23a", alpha: 0.45).cgColor); ctx.fill(f)
        case ".": break
        default: ctx.setFillColor(UIColor(white: 0.3, alpha: 0.6).cgColor); ctx.fill(f)
        }
    }

    /// a spring's pad on a surface (the course's, drawn in a maze's '^' cells too): `p` the middle of its base on the surface,
    /// `scale` points a metre; a dark base, a steel coil, a glowing plate with two chevrons, `fire` (0 to 1) the plate bobbing
    static func springPad(_ ctx: CGContext, _ p: CGPoint, scale: CGFloat, fire: CGFloat, color: UIColor) {
        let w = 0.55 * scale, base = 0.06 * scale, coilH = 0.22 * scale
        ctx.setFillColor(UIColor(hex: "#2b2f3a").cgColor)
        ctx.fill(CGRect(x: p.x - w * 0.55, y: p.y - base, width: w * 1.1, height: base))
        let top = p.y - base - coilH * (1 - 0.25 * fire)
        ctx.setStrokeColor(UIColor(hex: "#d9e2ef").cgColor); ctx.setLineWidth(max(1.2, 0.035 * scale))
        let turns = 5
        let coil = CGMutablePath()
        for k in 0...turns * 8 { // a helix seen side on: a zigzag of rounded turns
            let t = CGFloat(k) / CGFloat(turns * 8)
            let pt = CGPoint(x: p.x + sin(t * CGFloat(turns) * 2 * .pi) * w * 0.32, y: p.y - base - (p.y - base - top) * t)
            k == 0 ? coil.move(to: pt) : coil.addLine(to: pt)
        }
        ctx.addPath(coil); ctx.strokePath()
        let plate = CGRect(x: p.x - w * 0.5, y: top - 0.07 * scale, width: w, height: 0.07 * scale)
        ctx.setShadow(offset: .zero, blur: 8 + 8 * fire, color: color.cgColor)
        ctx.setFillColor(color.cgColor); ctx.fill(plate)
        ctx.setShadow(offset: .zero, blur: 0, color: nil)
        ctx.setStrokeColor(UIColor(hex: "#14301a").cgColor); ctx.setLineWidth(1.4)
        for k in 0..<2 { // chevrons on it: up and on
            let cx = plate.midX + CGFloat(k) * 6 - 3, cy = plate.midY
            ctx.strokeLineSegments(between: [CGPoint(x: cx - 3, y: cy + 1.5), CGPoint(x: cx, y: cy - 1.5), CGPoint(x: cx, y: cy - 1.5), CGPoint(x: cx + 3, y: cy + 1.5)])
        }
    }

    /// the lasers' beams in a maze's rows (top first): from each '(' or ')' along its row through the air, a laser
    /// pointer's dashed line with a dot where it ends (`cellRect(c, i)`: the cell's rect, i counted from the top)
    static func beams(_ ctx: CGContext, _ rows: [String], cols: ClosedRange<Int>, _ cellRect: (Int, Int) -> CGRect) {
        ctx.setStrokeColor(UIColor(hex: "#ff2b4a", alpha: 0.75).cgColor)
        ctx.setLineDash(phase: 0, lengths: [5, 4])
        for (i, row) in rows.enumerated() {
            let line = Array(row)
            for (c, ch) in line.enumerated() where ch == "(" || ch == ")" {
                let d = ch == ")" ? 1 : -1
                var e = c
                while e + d >= 0 && e + d < line.count && line[e + d] == "." { e += d }
                if e == c || max(c, e) < cols.lowerBound || min(c, e) > cols.upperBound { continue }
                let a = cellRect(c, i), b = cellRect(e, i)
                let from = CGPoint(x: d > 0 ? a.maxX : a.minX, y: a.midY), to = CGPoint(x: d > 0 ? b.maxX : b.minX, y: b.midY)
                ctx.setLineWidth(max(1, a.height * 0.06))
                ctx.strokeLineSegments(between: [from, to])
                ctx.setFillColor(UIColor(hex: "#ff2b4a").cgColor)
                ctx.fillEllipse(in: CGRect(x: to.x - 2.5, y: to.y - 2.5, width: 5, height: 5))
            }
        }
        ctx.setLineDash(phase: 0, lengths: [])
    }

    /// a saw blade: its top half over the surface it's set in (hub on that surface)
    static func blade(_ ctx: CGContext, _ hub: CGPoint, _ r: CGFloat, _ color: UIColor, spin: Double = 0) {
        ctx.saveGState()
        ctx.clip(to: CGRect(x: hub.x - r - 2, y: hub.y - r - 2, width: 2 * r + 4, height: r + 2)) // (its top half spins above the surface)
        let teeth = 12
        let p = CGMutablePath()
        for k in 0..<(teeth * 2) {
            let a = spin + Double(k) * .pi / Double(teeth)
            let rr = k % 2 == 0 ? r : r * 0.78
            let pt = CGPoint(x: hub.x + CGFloat(cos(a)) * rr, y: hub.y + CGFloat(sin(a)) * rr)
            if k == 0 { p.move(to: pt) } else { p.addLine(to: pt) }
        }
        p.closeSubpath()
        ctx.setShadow(offset: .zero, blur: 10, color: color.cgColor)
        ctx.setFillColor(UIColor(hex: "#d8dde6").cgColor)
        ctx.addPath(p); ctx.fillPath()
        ctx.setShadow(offset: .zero, blur: 0, color: nil)
        ctx.setStrokeColor(color.cgColor); ctx.setLineWidth(1.2); ctx.addPath(p); ctx.strokePath()
        ctx.setFillColor(color.cgColor)
        ctx.fillEllipse(in: CGRect(x: hub.x - r * 0.22, y: hub.y - r * 0.22, width: r * 0.44, height: r * 0.44))
        ctx.restoreGState()
    }

    static func label(_ ctx: CGContext, _ s: String, at p: CGPoint, color: UIColor, size: CGFloat = 11, weight: UIFont.Weight = .bold) {
        UIGraphicsPushContext(ctx)
        (s as NSString).draw(at: p, withAttributes: [.font: UIFont.systemFont(ofSize: size, weight: weight), .foregroundColor: color])
        UIGraphicsPopContext()
    }
}

final class MazeCanvasView: UIView, UIGestureRecognizerDelegate, UIPencilInteractionDelegate {
    private let cellsView = Painter()
    private let overlayView = Painter()

    // what it draws
    var scene: GridScene? { didSet { updateTicker(); if oldValue == nil || oldValue?.rows.count != scene?.rows.count { fitted = false; setNeedsLayout() }; redraw() } }
    var report: GridReport? { didSet { redraw() } }
    /// the chunks the last roll laid, outlined in magenta (they pulse a little) until the maze is edited
    var placed: [MazePlaced] = [] { didSet { updateTicker(); overlayView.setNeedsDisplay() } }
    var look = ZoneLook.of("egypt")
    var showWay = true, showReach = false, showMisses = true
    var tool = "brush"
    var hoverCell: (c: Int, i: Int)? { didSet { overlayView.setNeedsDisplay() } }

    // what it says
    var onPointer: ((Phase, Int, Int) -> Void)?
    var onErase: ((Bool) -> Void)?
    var onPencilTap: ((UIPencilPreferredAction) -> Void)?

    // the view: the cell at its top left (columns, row indexes from the top), points a cell
    private var originC: Double = -1
    private var originI: Double = -1
    private(set) var z: Double = 18
    private var fitted = false
    private let toolGesture = ToolGestureRecognizer()
    private weak var pan: UIPanGestureRecognizer?
    private var pencilOnly = false
    static let rulerTop: CGFloat = 22
    static let rulerLeft: CGFloat = 30

    override init(frame: CGRect) {
        super.init(frame: frame)
        isOpaque = true
        backgroundColor = UIColor(hex: "#07040c")
        isMultipleTouchEnabled = true
        for v in [cellsView, overlayView] { v.frame = bounds; addSubview(v) }
        cellsView.paint = { [unowned self] ctx in self.drawCells(ctx) }
        overlayView.paint = { [unowned self] ctx in self.drawOverlay(ctx) }
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
    }
    required init?(coder: NSCoder) { fatalError() }

    override var canBecomeFirstResponder: Bool { true }
    override func didMoveToWindow() { super.didMoveToWindow(); if window != nil { becomeFirstResponder() }; updateTicker() }

    func redraw() {
        cellsView.setNeedsDisplay()
        overlayView.setNeedsDisplay()
    }

    // a spring's plate bobs: while the maze has a spring in it and the view is on screen, the cells redraw 30 times a second
    private var time: Double = 0
    private var ticker: CADisplayLink?
    private func updateTicker() {
        let wanted = window != nil && ((scene?.rows.contains { $0.contains("^") } ?? false) || !placed.isEmpty)
        if wanted, ticker == nil {
            let l = CADisplayLink(target: self, selector: #selector(tick(_:)))
            l.preferredFramesPerSecond = 30
            l.add(to: .main, forMode: .common)
            ticker = l
        } else if !wanted {
            ticker?.invalidate()
            ticker = nil
        }
    }
    @objc private func tick(_ l: CADisplayLink) {
        time = l.timestamp
        cellsView.setNeedsDisplay()
        overlayView.setNeedsDisplay() // (the bot's way: a spring's flight dotted, marching)
    }

    // MARK: the view

    private var rowCount: Int { scene?.rows.count ?? 0 }
    private var colCount: Int { scene?.cols ?? 0 }

    func cellRect(_ c: Int, _ i: Int) -> CGRect {
        CGRect(x: Self.rulerLeft + CGFloat((Double(c) - originC) * z), y: Self.rulerTop + CGFloat((Double(i) - originI) * z), width: CGFloat(z), height: CGFloat(z))
    }
    func point(_ c: Double, _ i: Double) -> CGPoint {
        CGPoint(x: Self.rulerLeft + CGFloat((c - originC) * z), y: Self.rulerTop + CGFloat((i - originI) * z))
    }
    func cellAt(_ p: CGPoint) -> (c: Int, i: Int) {
        (Int(floor(originC + Double(p.x - Self.rulerLeft) / z)), Int(floor(originI + Double(p.y - Self.rulerTop) / z)))
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        if !fitted, rowCount > 0, bounds.height > 80 { fit() }
        redraw()
    }

    /// the whole maze's height in view, its entry at the left
    func fit() {
        guard rowCount > 0 else { return }
        let h = Double(bounds.height - Self.rulerTop - 12), w = Double(bounds.width - Self.rulerLeft - 12)
        z = max(6, min(42, h / Double(rowCount), w / 12))
        originI = max(-0.5, (Double(rowCount) - h / z) / 2)
        originC = -0.5
        fitted = true
        clamp()
        redraw()
    }

    /// a cell to the middle of the view (a checklist item, a near miss)
    func center(on c: Int, _ i: Int) {
        originC = Double(c) - Double(bounds.width - Self.rulerLeft) / z / 2
        originI = Double(i) - Double(bounds.height - Self.rulerTop) / z / 2
        clamp()
        redraw()
    }

    func zoom(by k: Double, around p: CGPoint? = nil) {
        let at = p ?? CGPoint(x: bounds.midX, y: bounds.midY)
        let c = originC + Double(at.x - Self.rulerLeft) / z, i = originI + Double(at.y - Self.rulerTop) / z
        z = max(4, min(80, z * k))
        originC = c - Double(at.x - Self.rulerLeft) / z
        originI = i - Double(at.y - Self.rulerTop) / z
        clamp()
        redraw()
    }

    private func clamp() {
        let vw = Double(bounds.width - Self.rulerLeft) / z, vh = Double(bounds.height - Self.rulerTop) / z
        originC = max(-4, min(Double(colCount) + 4 - vw, originC))
        originI = max(-4, min(Double(rowCount) + 4 - vh, originI))
        if vh > Double(rowCount) + 8 { originI = (Double(rowCount) - vh) / 2 }
    }

    private func scroll(by d: CGPoint) {
        originC -= Double(d.x) / z
        originI -= Double(d.y) / z
        clamp()
        redraw()
    }

    // MARK: gestures

    private var toolBegun = false
    private var panLast: CGPoint?

    @objc private func toolMoved(_ g: ToolGestureRecognizer) {
        if g.touchType == .pencil && !pencilOnly && traitCollection.userInterfaceIdiom == .pad { usePencilOnly(true) }
        if tool == "pan" { // (one finger drags the maze along)
            switch g.state {
            case .began: panLast = g.startLocation; fallthrough
            case .changed:
                if let last = panLast { scroll(by: CGPoint(x: g.location.x - last.x, y: g.location.y - last.y)) }
                panLast = g.location
            default: panLast = nil
            }
            return
        }
        let w = cellAt(g.location), start = cellAt(g.startLocation)
        switch g.state {
        case .began:
            toolBegun = true
            onPointer?(.begin, start.c, start.i)
            if w != start { onPointer?(.move, w.c, w.i) }
        case .changed: onPointer?(.move, w.c, w.i); autoScroll(g.location)
        case .ended:
            if !toolBegun { onPointer?(.begin, start.c, start.i) }
            onPointer?(.end, w.c, w.i)
            toolBegun = false
        case .cancelled, .failed:
            if toolBegun { onPointer?(.cancel, w.c, w.i) }
            toolBegun = false
        default: break
        }
    }

    func usePencilOnly(_ on: Bool) {
        pencilOnly = on
        toolGesture.allowedTouchTypes = on ? [NSNumber(value: UITouch.TouchType.pencil.rawValue)] : [NSNumber(value: UITouch.TouchType.direct.rawValue), NSNumber(value: UITouch.TouchType.pencil.rawValue)]
        pan?.minimumNumberOfTouches = on ? 1 : 2
    }

    private static let navigationTouches = [NSNumber(value: UITouch.TouchType.direct.rawValue), NSNumber(value: UITouch.TouchType.indirectPointer.rawValue)]

    private func autoScroll(_ p: CGPoint) { // (a drag at an edge carries the maze along)
        let edge: CGFloat = 36
        var d = CGPoint.zero
        if p.x > bounds.width - edge { d.x = -6 } else if p.x < Self.rulerLeft + edge { d.x = 6 }
        if p.y > bounds.height - edge { d.y = -6 } else if p.y < Self.rulerTop + edge { d.y = 6 }
        if d != .zero { scroll(by: d) }
    }

    @objc private func panned(_ g: UIPanGestureRecognizer) {
        let d = g.translation(in: self)
        g.setTranslation(.zero, in: self)
        scroll(by: d)
    }

    @objc private func pinched(_ g: UIPinchGestureRecognizer) {
        zoom(by: Double(g.scale), around: g.location(in: self))
        g.scale = 1
    }

    @objc private func hovered(_ g: UIHoverGestureRecognizer) {
        switch g.state {
        case .began, .changed: let c = cellAt(g.location(in: self)); hoverCell = (c.c, c.i)
        default: hoverCell = nil
        }
    }

    func gestureRecognizer(_ g: UIGestureRecognizer, shouldRecognizeSimultaneouslyWith o: UIGestureRecognizer) -> Bool { true }

    func pencilInteractionDidTap(_ interaction: UIPencilInteraction) { onPencilTap?(UIPencilInteraction.preferredTapAction) }
    @available(iOS 17.5, *)
    func pencilInteraction(_ interaction: UIPencilInteraction, didReceiveTap tap: UIPencilInteraction.Tap) { onPencilTap?(UIPencilInteraction.preferredTapAction) }

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        if presses.contains(where: { $0.key?.keyCode == .keyboardLeftAlt || $0.key?.keyCode == .keyboardRightAlt }) { onErase?(true) }
        super.pressesBegan(presses, with: event)
    }
    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        if presses.contains(where: { $0.key?.keyCode == .keyboardLeftAlt || $0.key?.keyCode == .keyboardRightAlt }) { onErase?(false) }
        super.pressesEnded(presses, with: event)
    }

    // MARK: drawing

    private var visible: (c0: Int, c1: Int, i0: Int, i1: Int) {
        (max(0, Int(floor(originC)) - 1), min(colCount - 1, Int(ceil(originC + Double(bounds.width - Self.rulerLeft) / z)) + 1),
         max(0, Int(floor(originI)) - 1), min(rowCount - 1, Int(ceil(originI + Double(bounds.height - Self.rulerTop) / z)) + 1))
    }

    /// the cells, and where she gets under them (redrawn when the maze or the view changes)
    private func drawCells(_ ctx: CGContext) {
        guard let s = scene, !s.rows.isEmpty else { return }
        let v = visible
        guard v.c0 <= v.c1, v.i0 <= v.i1 else { return }
        let all = cellRect(0, 0).union(cellRect(s.cols - 1, s.rows.count - 1))
        ctx.setFillColor(UIColor(hex: "#0b0716").cgColor)
        ctx.fill(all)
        let fresh = report.map { ($0.rows ?? -1) == s.rows.count && ($0.cols ?? -1) == s.cols } ?? false
        if showReach, fresh, let map = report?.cellMap { // (green: on the way through; red: a dead end)
            for i in v.i0...v.i1 where i < map.count {
                let line = Array(map[i])
                for c in v.c0...v.c1 where c < line.count && line[c] != "." {
                    ctx.setFillColor((line[c] == "w" ? UIColor(hex: "#3dff8a", alpha: 0.16) : UIColor(hex: "#ff2b4a", alpha: 0.2)).cgColor)
                    ctx.fill(cellRect(c, i))
                }
            }
        }
        for i in v.i0...v.i1 {
            let line = Array(s.rows[i])
            for c in v.c0...v.c1 where c < line.count && line[c] != "." {
                MazePaint.cell(ctx, line[c], cellRect(c, i).insetBy(dx: 0.5, dy: 0.5), look, time: time)
            }
        }
        MazePaint.beams(ctx, s.rows, cols: v.c0...v.c1) { cellRect($0, $1) }
        if z > 9 { // the grid's lines, faint
            ctx.setStrokeColor(UIColor(white: 1, alpha: 0.05).cgColor)
            ctx.setLineWidth(1)
            for c in v.c0...(v.c1 + 1) { let x = cellRect(c, 0).minX; ctx.strokeLineSegments(between: [CGPoint(x: x, y: max(all.minY, 0)), CGPoint(x: x, y: min(all.maxY, bounds.height))]) }
            for i in v.i0...(v.i1 + 1) { let y = cellRect(0, i).minY; ctx.strokeLineSegments(between: [CGPoint(x: max(all.minX, 0), y: y), CGPoint(x: min(all.maxX, bounds.width), y: y)]) }
        }
        ctx.setStrokeColor(UIColor(hex: "#ffc23a", alpha: 0.9).cgColor)
        ctx.setShadow(offset: .zero, blur: 10, color: UIColor(hex: "#ffc23a").cgColor)
        ctx.setLineWidth(1.5)
        ctx.stroke(all)
        ctx.setShadow(offset: .zero, blur: 0, color: nil)
        // the doors: a glowing arc where she comes in and goes out
        for (door, color, dir) in [(s.entry, UIColor(hex: "#3dff8a"), -1.0), (s.exit, UIColor(hex: "#ffc23a"), 1.0)] {
            guard let d = door, d.count == 2 else { continue }
            let f = cellRect(d[0], d[1])
            let mid = CGPoint(x: dir < 0 ? f.minX : f.maxX, y: f.minY)
            ctx.setStrokeColor(color.cgColor)
            ctx.setShadow(offset: .zero, blur: 12, color: color.cgColor)
            ctx.setLineWidth(3)
            ctx.addArc(center: mid, radius: f.height * 1.05, startAngle: dir < 0 ? .pi / 2 : -.pi / 2, endAngle: dir < 0 ? 1.5 * .pi : .pi / 2, clockwise: false)
            ctx.strokePath()
            ctx.setShadow(offset: .zero, blur: 0, color: nil)
        }
    }

    /// the checker's marks, the tool's preview, the start, the hover, the rulers
    private func drawOverlay(_ ctx: CGContext) {
        guard let s = scene, !s.rows.isEmpty else { return }
        let n = s.rows.count
        let fresh = report.map { ($0.rows ?? -1) == n && ($0.cols ?? -1) == s.cols } ?? false
        if fresh, let r = report {
            if showWay, let way = r.way, way.count > 1 { // the bot's way: cyan running right, pink running left; a dot where she lays
                ctx.setLineWidth(max(1.5, CGFloat(z) * 0.12))
                ctx.setLineCap(.round)
                ctx.setLineJoin(.round)
                for k in 1..<way.count {
                    let a = way[k - 1], b = way[k]
                    let pa = point(Double(a[0]) + 0.5, Double(n - 1 - (a[1] + a[2])) + 0.5), pb = point(Double(b[0]) + 0.5, Double(n - 1 - (b[1] + b[2])) + 0.5)
                    if a.count > 8, a[8] & 16 != 0 { // flown off a spring: her arc, up 5 rows and over to where she lands, lime and dotted
                        let sx = pa.x + CGFloat(a[3] > 0 ? z : -z), apex = min(pa.y, pb.y) - CGFloat(5 * z)
                        let path = CGMutablePath()
                        path.move(to: pa); path.addLine(to: CGPoint(x: sx, y: pa.y))
                        path.addQuadCurve(to: pb, control: CGPoint(x: (sx + pb.x) / 2, y: 2 * apex - 0.5 * (pa.y + pb.y)))
                        ctx.saveGState()
                        ctx.setStrokeColor(UIColor(hex: "#7dff4a", alpha: 0.9).cgColor)
                        ctx.setLineDash(phase: CGFloat(-time * 24), lengths: [1, max(4, CGFloat(z) * 0.35)])
                        ctx.addPath(path); ctx.strokePath()
                        ctx.restoreGState()
                        ctx.setFillColor(UIColor(hex: "#7dff4a").cgColor)
                        let rr = max(2.5, CGFloat(z) * 0.18)
                        ctx.fillEllipse(in: CGRect(x: pb.x - rr, y: pb.y - rr, width: 2 * rr, height: 2 * rr))
                    } else {
                        ctx.setStrokeColor((a[3] > 0 ? UIColor(hex: "#29e7ff", alpha: 0.85) : UIColor(hex: "#ff2bd6", alpha: 0.85)).cgColor)
                        ctx.strokeLineSegments(between: [pa, pb])
                    }
                    if a[4] > 0 {
                        ctx.setFillColor(UIColor(Neon.egg).cgColor)
                        let rr = max(2, CGFloat(z) * 0.14)
                        ctx.fillEllipse(in: CGRect(x: pa.x - rr, y: pa.y - rr, width: 2 * rr, height: 2 * rr))
                    }
                }
            }
            if showMisses, let misses = r.wayNearMisses { // PERFECT, CLOSE SHAVE, CLOSE CALL
                for m in misses where m.count >= 3 {
                    let f = cellRect(m[0], n - 1 - m[1])
                    let (text, color): (String, UIColor) = m[2] == 0 ? ("P", UIColor(hex: "#ffc23a")) : m[2] == 1 ? ("S", UIColor(hex: "#ff3d6a")) : ("C", UIColor(hex: "#29e7ff"))
                    let rr = max(6, min(11, CGFloat(z) * 0.35))
                    let b = CGRect(x: f.midX - rr, y: f.minY - rr * 0.6, width: 2 * rr, height: 2 * rr)
                    ctx.setFillColor(color.cgColor)
                    ctx.setShadow(offset: .zero, blur: 6, color: color.cgColor)
                    ctx.fillEllipse(in: b)
                    ctx.setShadow(offset: .zero, blur: 0, color: nil)
                    if rr >= 7 { MazePaint.label(ctx, text, at: CGPoint(x: b.midX - rr * 0.38, y: b.midY - rr * 0.62), color: .black, size: rr * 1.1, weight: .heavy) }
                }
            }
            var crashes: [(Double, Double)] = []
            if let m = r.meter, m.count > 1, !m[1].out, let c = m[1].crash, c.count == 3 { crashes.append((Double(c[0]), Double(c[1] + c[2]))) }
            if let g = r.game, !g.out, let c = g.crash, c.count == 2 { crashes.append((c[0], c[1])) }
            for (c, b) in crashes { // where she ran dry, or the game's bot crashed
                let p = point(c + 0.5, Double(n - 1) - b + 0.5)
                let rr = max(8, CGFloat(z) * 0.6)
                ctx.setStrokeColor(UIColor(hex: "#ff2b4a").cgColor)
                ctx.setShadow(offset: .zero, blur: 10, color: UIColor(hex: "#ff2b4a").cgColor)
                ctx.setLineWidth(3)
                ctx.strokeLineSegments(between: [CGPoint(x: p.x - rr, y: p.y - rr), CGPoint(x: p.x + rr, y: p.y + rr), CGPoint(x: p.x + rr, y: p.y - rr), CGPoint(x: p.x - rr, y: p.y + rr)])
                ctx.setShadow(offset: .zero, blur: 0, color: nil)
            }
        }
        for u in placed where u.r0 >= 0 && u.r1 < n && u.c0 >= 0 && u.c1 < s.cols { // the chunks a roll laid: magenta, pulsing, named
            let f = cellRect(u.c0, u.r0).union(cellRect(u.c1, u.r1)).insetBy(dx: -1, dy: -1)
            let pulse = 0.55 + 0.45 * (0.5 + 0.5 * sin(time * 3))
            let magenta = UIColor(hex: "#ff2bd6")
            ctx.setFillColor(magenta.withAlphaComponent(0.07 * pulse).cgColor)
            ctx.fill(f)
            ctx.setStrokeColor(magenta.withAlphaComponent(pulse).cgColor)
            ctx.setShadow(offset: .zero, blur: 8, color: magenta.cgColor)
            ctx.setLineWidth(2)
            ctx.stroke(f)
            ctx.setShadow(offset: .zero, blur: 0, color: nil)
            MazePaint.label(ctx, u.id + (u.mirrored ? " \u{21C4}" : ""), at: CGPoint(x: f.minX + 3, y: max(Self.rulerTop + 2, f.minY - 17)), color: magenta, size: 12, weight: .bold)
        }
        if let p = s.preview { // the tool's gesture: dashed
            let f = cellRect(p.c0, p.i0).union(cellRect(p.c1, p.i1))
            let color = p.erase ? UIColor(hex: "#ff6a80") : UIColor(hex: "#29e7ff")
            ctx.setFillColor(color.withAlphaComponent(0.14).cgColor)
            ctx.fill(f)
            ctx.setStrokeColor(color.cgColor)
            ctx.setLineWidth(2)
            ctx.setLineDash(phase: 0, lengths: [6, 4])
            ctx.stroke(f)
            ctx.setLineDash(phase: 0, lengths: [])
            let what = p.kind == "cols" ? "\(p.c1 - p.c0 + 1) col" + (p.c1 > p.c0 ? "s" : "") : p.kind == "rows" ? "\(p.i1 - p.i0 + 1) row" + (p.i1 > p.i0 ? "s" : "") : "\(p.c1 - p.c0 + 1) × \(p.i1 - p.i0 + 1)"
            MazePaint.label(ctx, (p.erase ? "− " : "+ ") + what, at: CGPoint(x: f.minX + 4, y: max(Self.rulerTop + 2, f.minY - 18)), color: color, size: 13)
        }
        if let st = s.start, st.count == 2 { // the play-test's start: a green flag on her cell
            let f = cellRect(st[0], st[1])
            let pole = CGPoint(x: f.minX + f.width * 0.3, y: f.maxY)
            ctx.setStrokeColor(UIColor.white.cgColor)
            ctx.setLineWidth(2)
            ctx.strokeLineSegments(between: [pole, CGPoint(x: pole.x, y: f.minY - f.height * 0.6)])
            let flag = CGMutablePath()
            flag.move(to: CGPoint(x: pole.x, y: f.minY - f.height * 0.6))
            flag.addLine(to: CGPoint(x: pole.x + f.width * 0.8, y: f.minY - f.height * 0.3))
            flag.addLine(to: CGPoint(x: pole.x, y: f.minY))
            ctx.setFillColor(UIColor(hex: "#3dff8a").cgColor)
            ctx.setShadow(offset: .zero, blur: 8, color: UIColor(hex: "#3dff8a").cgColor)
            ctx.addPath(flag); ctx.fillPath()
            ctx.setShadow(offset: .zero, blur: 0, color: nil)
        }
        if let h = hoverCell, h.c >= 0, h.c < s.cols, h.i >= 0, h.i < n { // a hovering Pencil: where it lands
            ctx.setStrokeColor(UIColor(white: 1, alpha: 0.8).cgColor)
            ctx.setLineWidth(1.5)
            ctx.stroke(cellRect(h.c, h.i).insetBy(dx: 1, dy: 1))
        }
        drawRulers(ctx, s)
    }

    /// columns along the top, the game's rows (0 at the street) down the left
    private func drawRulers(_ ctx: CGContext, _ s: GridScene) {
        ctx.setFillColor(UIColor(hex: "#07040c", alpha: 0.92).cgColor)
        ctx.fill(CGRect(x: 0, y: 0, width: bounds.width, height: Self.rulerTop))
        ctx.fill(CGRect(x: 0, y: 0, width: Self.rulerLeft, height: bounds.height))
        let v = visible
        guard v.c0 <= v.c1, v.i0 <= v.i1 else { return }
        let every = z >= 26 ? 1 : z >= 13 ? 5 : z >= 6 ? 10 : 20
        let tint = UIColor(look.accent).withAlphaComponent(0.75)
        for c in v.c0...v.c1 where c % every == 0 {
            let f = cellRect(c, 0)
            guard f.midX > Self.rulerLeft else { continue }
            MazePaint.label(ctx, "\(c)", at: CGPoint(x: f.minX + 2, y: 4), color: tint, size: 10, weight: .semibold)
        }
        let n = s.rows.count
        let rowEvery = z >= 13 ? 1 : 5
        for i in v.i0...v.i1 where (n - 1 - i) % rowEvery == 0 {
            let f = cellRect(0, i)
            guard f.midY > Self.rulerTop else { continue }
            MazePaint.label(ctx, "\(n - 1 - i)", at: CGPoint(x: 4, y: f.midY - 7), color: tint, size: 10, weight: .semibold)
        }
    }
}

struct MazeCanvas: UIViewRepresentable {
    @ObservedObject var maze: MazeModel

    func makeUIView(context: Context) -> MazeCanvasView {
        let v = MazeCanvasView(frame: .zero)
        maze.canvas = v
        v.onPointer = { [weak maze] phase, c, i in maze?.pointer(phase, col: c, row: i) }
        v.onErase = { [weak maze] on in maze?.erase = on }
        v.onPencilTap = { [weak maze] action in
            guard let maze else { return }
            if action == .switchEraser || action == .switchPrevious { withAnimation(.spring(response: 0.3)) { maze.erase.toggle() }; Haptic.tap() }
        }
        return v
    }

    func updateUIView(_ v: MazeCanvasView, context: Context) {
        v.look = ZoneLook.of(maze.host.zone)
        v.tool = maze.tool
        v.showWay = maze.showWay
        v.showReach = maze.showReach
        v.showMisses = maze.showMisses
        v.scene = maze.scene
        v.report = maze.report.map { maze.canvasReport($0) }
    }
}
