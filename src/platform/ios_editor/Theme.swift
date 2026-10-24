// The editor's look: the game's neon night (each zone's palette, as the web editor and the game draw it), glass
// panels edged in the zone's light, and buttons that glow and spring when they're picked.
import SwiftUI
import UIKit

struct ZoneLook {
    let sky: [Color]
    let accent: Color
    let crate, crateLit, ground: Color
    let crateEdge: Color
    let uiAccent: UIColor
    static func of(_ zone: String) -> ZoneLook {
        switch zone {
        case "egypt":
            return ZoneLook(sky: [Color(hex: "#07040c"), Color(hex: "#1d1124")], accent: Color(hex: "#ffc23a"), crate: Color(hex: "#22170b"),
                            crateLit: Color(hex: "#2f2010"), ground: Color(hex: "#0a0604"), crateEdge: Color(hex: "#e2a43c", opacity: 0.7), uiAccent: UIColor(hex: "#ffc23a"))
        case "japan":
            return ZoneLook(sky: [Color(hex: "#08030c"), Color(hex: "#230a22")], accent: Color(hex: "#ff2bd6"), crate: Color(hex: "#200a1d"),
                            crateLit: Color(hex: "#2c0f28"), ground: Color(hex: "#08030a"), crateEdge: Color(hex: "#ff2bd6", opacity: 0.55), uiAccent: UIColor(hex: "#ff2bd6"))
        default:
            return ZoneLook(sky: [Color(hex: "#06030e"), Color(hex: "#120a26")], accent: Color(hex: "#29e7ff"), crate: Color(hex: "#0e1a2c"),
                            crateLit: Color(hex: "#13243c"), ground: Color(hex: "#05030b"), crateEdge: Color(hex: "#29e7ff", opacity: 0.55), uiAccent: UIColor(hex: "#29e7ff"))
        }
    }
    static func zoneName(_ zone: String) -> String { zone == "egypt" ? "Neon Egypt" : zone == "japan" ? "Neon Japan" : "The neon city" }
}

enum Neon {
    static let night = Color(hex: "#06030e")
    static let panel = Color(hex: "#120a26")
    static let magenta = Color(hex: "#ff2bd6")
    static let cyan = Color(hex: "#29e7ff")
    static let lime = Color(hex: "#3dff8a")
    static let amber = Color(hex: "#ffb43a")
    static let red = Color(hex: "#ff2b4a")
    static let egg = Color(hex: "#fff3dc")
    static let hay = Color(hex: "#4a3510")
    static let hayLit = Color(hex: "#5a4114")
    static let straw = Color(hex: "#ffc23a")
}

/// a frosted panel edged in light
struct GlassPanel: ViewModifier {
    var accent: Color
    var radius: CGFloat = 18
    func body(content: Content) -> some View {
        content
            .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: radius, style: .continuous))
            .background(Neon.panel.opacity(0.55), in: RoundedRectangle(cornerRadius: radius, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: radius, style: .continuous)
                    .strokeBorder(LinearGradient(colors: [accent.opacity(0.65), accent.opacity(0.12), Neon.magenta.opacity(0.35)],
                                                 startPoint: .topLeading, endPoint: .bottomTrailing), lineWidth: 1)
            )
            .shadow(color: accent.opacity(0.18), radius: 16, y: 6)
    }
}

extension View {
    func glass(_ accent: Color, radius: CGFloat = 18) -> some View { modifier(GlassPanel(accent: accent, radius: radius)) }
}

/// a tool's button: its symbol, glowing in the zone's light when it's the tool, springing as it's picked
struct ToolButton: View {
    let symbol: String
    let label: String
    let key: String
    let tint: Color
    let selected: Bool
    var compact = false
    var namespace: Namespace.ID
    let action: () -> Void

    var body: some View {
        Button(action: {
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
            action()
        }) {
            ToolFace(symbol: symbol, label: label, tint: tint, selected: selected, compact: compact, namespace: namespace)
        }
        .buttonStyle(PressSpring())
        .accessibilityLabel(label)
        .accessibilityHint("Key \(key.uppercased())")
    }
}

/// what a tool's button shows: its symbol (the glow behind it when it's the tool) and its name; `more`: a group's
/// corner mark (there's more in it)
struct ToolFace: View {
    let symbol: String
    let label: String
    let tint: Color
    let selected: Bool
    var compact = false
    var more = false
    var namespace: Namespace.ID

    var body: some View {
        VStack(spacing: 3) {
            ZStack {
                if selected {
                    RoundedRectangle(cornerRadius: 12, style: .continuous)
                        .fill(RadialGradient(colors: [tint.opacity(0.55), tint.opacity(0.08)], center: .center, startRadius: 2, endRadius: 30))
                        .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(tint.opacity(0.9), lineWidth: 1.2))
                        .shadow(color: tint.opacity(0.8), radius: 10)
                        .matchedGeometryEffect(id: "toolGlow", in: namespace)
                }
                Image(systemName: symbol)
                    .font(.system(size: compact ? 18 : 20, weight: .semibold))
                    .symbolRenderingMode(.hierarchical)
                    .foregroundStyle(selected ? Color.white : tint.opacity(0.85))
                    .shadow(color: selected ? tint : .clear, radius: 6)
                    .symbolEffect(.bounce, value: selected)
                    .contentTransition(.symbolEffect(.replace))
            }
            .frame(width: compact ? 42 : 46, height: compact ? 38 : 42)
            .overlay(alignment: .bottomTrailing) { if more { GroupMark(tint: selected ? .white : tint) } }
            if !compact {
                Text(label).font(.system(size: 10, weight: .semibold, design: .rounded)).foregroundStyle(selected ? .white : .white.opacity(0.6)).lineLimit(1)
            }
        }
        .contentShape(Rectangle())
    }
}

/// a tool group's corner mark: a little wedge in its corner, saying there's more under it
struct GroupMark: View {
    let tint: Color
    var body: some View {
        Path { p in p.move(to: CGPoint(x: 7, y: 0)); p.addLine(to: CGPoint(x: 7, y: 7)); p.addLine(to: CGPoint(x: 0, y: 7)); p.closeSubpath() }
            .fill(tint.opacity(0.8))
            .frame(width: 7, height: 7)
            .shadow(color: tint.opacity(0.6), radius: 2)
            .padding(3)
    }
}

/// A group of tools behind one button (the Hazards, Build, Edit ...): it wears the one last used. A tap picks that one;
/// tapped again while it's the tool, or held, the group's flyout opens beside it (beside the rail; over the strip)
struct ToolGroupButton<Face: View, Flyout: View>: View {
    let label: String
    let selected: Bool
    var vertical = true
    let face: Face
    let pick: () -> Void
    @ViewBuilder let flyout: (_ close: @escaping () -> Void) -> Flyout
    @State var open = false
    @State var held = false // (a hold opened it: the tap its release makes isn't a pick)

    var body: some View {
        Button {
            if held { held = false; return }
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
            if selected { open = true } else { pick() }
        } label: { face }
        .buttonStyle(PressSpring())
        .simultaneousGesture(LongPressGesture(minimumDuration: 0.4).onEnded { _ in
            held = true; open = true; Haptic.tap()
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.8) { held = false }
        })
        // (arrowEdge: the popover's own edge, so it opens beside the rail, over the strip)
        .popover(isPresented: $open, attachmentAnchor: .rect(.bounds), arrowEdge: vertical ? .leading : .bottom) {
            flyout { open = false }.presentationCompactAdaptation(.popover)
        }
        .accessibilityLabel(label)
        .accessibilityHint("Tap again, or hold, for the rest of the \(label.lowercased())")
    }
}

/// a group's flyout: its name, then its tools, one a row
struct FlyoutPanel<Rows: View>: View {
    let title: String
    let accent: Color
    @ViewBuilder let rows: Rows
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title.uppercased()).font(.system(size: 11, weight: .heavy, design: .rounded)).kerning(1.4)
                .foregroundStyle(accent).padding(.horizontal, 10).padding(.top, 4).padding(.bottom, 2)
            rows
        }
        .padding(10)
        .frame(width: 320, alignment: .leading)
        .presentationBackground(.ultraThinMaterial)
        .background(LinearGradient(colors: [accent.opacity(0.16), .clear], startPoint: .top, endPoint: .bottom))
    }
}

/// one tool in a flyout: its icon, name, key and what it does; the tool's row glowing; rows springing in one after
/// another as the flyout opens
struct FlyoutRow<Icon: View, Trailing: View>: View {
    let index: Int
    let name: String
    let key: String
    let tip: String
    let tint: Color
    let selected: Bool
    @ViewBuilder let icon: Icon
    @ViewBuilder var trailing: Trailing
    let action: () -> Void
    @State var shown = false

    var body: some View {
        HStack(spacing: 10) {
            Button {
                Haptic.tap()
                action()
            } label: {
                HStack(spacing: 12) {
                    icon.frame(width: 36, height: 36)
                    VStack(alignment: .leading, spacing: 2) {
                        HStack(spacing: 6) {
                            Text(name).font(.system(size: 15, weight: .semibold, design: .rounded)).foregroundStyle(.white)
                            Spacer(minLength: 0)
                            if !key.isEmpty { // (a tool with no key: no keycap)
                                Text(key.uppercased()).font(.system(size: 11, weight: .heavy, design: .monospaced))
                                    .foregroundStyle(.white.opacity(0.75))
                                    .padding(.horizontal, 6).padding(.vertical, 2)
                                    .background(RoundedRectangle(cornerRadius: 5).fill(.white.opacity(0.08)))
                                    .overlay(RoundedRectangle(cornerRadius: 5).strokeBorder(.white.opacity(0.25), lineWidth: 0.8))
                            }
                        }
                        Text(tip).font(.system(size: 11, design: .rounded)).foregroundStyle(.white.opacity(0.6)).lineLimit(2)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(PressSpring())
            trailing
        }
        .padding(.horizontal, 10).padding(.vertical, 8)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(selected ? RadialGradient(colors: [tint.opacity(0.4), tint.opacity(0.06)], center: .leading, startRadius: 4, endRadius: 220)
                               : RadialGradient(colors: [.clear, .clear], center: .center, startRadius: 0, endRadius: 1))
                .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(selected ? tint.opacity(0.9) : .white.opacity(0.06), lineWidth: selected ? 1.2 : 1))
                .shadow(color: selected ? tint.opacity(0.5) : .clear, radius: 8)
        )
        .scaleEffect(shown ? 1 : 0.92, anchor: .leading)
        .opacity(shown ? 1 : 0)
        .offset(x: shown ? 0 : -10)
        .onAppear { withAnimation(.spring(response: 0.34, dampingFraction: 0.72).delay(0.025 * Double(index))) { shown = true } }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(name)
        .accessibilityHint(tip)
    }
}

extension FlyoutRow where Trailing == EmptyView {
    init(index: Int, name: String, key: String, tip: String, tint: Color, selected: Bool, @ViewBuilder icon: () -> Icon, action: @escaping () -> Void) {
        self.init(index: index, name: name, key: key, tip: tip, tint: tint, selected: selected, icon: icon, trailing: { EmptyView() }, action: action)
    }
}

/// a pill that pulses while something's under way (the bot), then settles on its verdict (still: an animation that
/// never stops keeps the iPad drawing, and warm)
struct StatusPill: View {
    let text: String
    let symbol: String
    let tint: Color
    var busy = false
    var body: some View {
        HStack(spacing: 6) {
            Image(systemName: symbol)
                .font(.system(size: 13, weight: .bold))
                .symbolEffect(.pulse, isActive: busy)
                .contentTransition(.symbolEffect(.replace))
            Text(text).font(.system(size: 13, weight: .semibold, design: .rounded)).monospacedDigit().contentTransition(.numericText())
        }
        .foregroundStyle(tint)
        .padding(.horizontal, 10).padding(.vertical, 6)
        .background(
            Capsule().fill(tint.opacity(0.14))
                .overlay(Capsule().strokeBorder(tint.opacity(busy ? 0.9 : 0.45), lineWidth: 1))
                .shadow(color: tint.opacity(busy ? 0.5 : 0.2), radius: busy ? 8 : 4)
        )
        .animation(.spring(response: 0.35, dampingFraction: 0.7), value: text)
    }
}

/// the night sky behind every screen: the zone's gradient, a faint grid, a horizon glow. Still: it's behind the frosted
/// panels, and anything moving under them has them blurred again every frame
struct NightBackdrop: View {
    let zone: String
    var body: some View {
        let look = ZoneLook.of(zone)
        ZStack {
            LinearGradient(colors: look.sky, startPoint: .top, endPoint: .bottom)
            RadialGradient(colors: [look.accent.opacity(0.17), .clear], center: .bottom, startRadius: 10, endRadius: 520)
            Canvas { ctx, size in
                let step: CGFloat = 28
                var p = Path()
                var x: CGFloat = 0
                while x < size.width { p.move(to: CGPoint(x: x, y: size.height * 0.55)); p.addLine(to: CGPoint(x: x, y: size.height)); x += step }
                var y = size.height * 0.55
                while y < size.height { p.move(to: CGPoint(x: 0, y: y)); p.addLine(to: CGPoint(x: size.width, y: y)); y += step * 0.6 }
                ctx.stroke(p, with: .color(look.accent.opacity(0.06)), lineWidth: 1)
            }
        }
        .ignoresSafeArea()
        .drawingGroup() // (drawn once into one layer)
    }
}

enum Haptic {
    static func tap() { UIImpactFeedbackGenerator(style: .light).impactOccurred() }
    static func success() { UINotificationFeedbackGenerator().notificationOccurred(.success) }
    static func warning() { UINotificationFeedbackGenerator().notificationOccurred(.warning) }
}

/// a button that dips as it's pressed and springs back (the button's own pressed state: a scroll view around it still
/// scrolls when the finger drags, where a gesture of its own would fight it)
struct PressSpring: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .scaleEffect(configuration.isPressed ? 0.9 : 1)
            .animation(.spring(response: 0.2, dampingFraction: 0.6), value: configuration.isPressed)
    }
}
