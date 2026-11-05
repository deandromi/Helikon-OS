import AppKit
import SwiftUI

// MARK: - Palette

enum HColor {
    static let posterPaper = Color(hex: 0xEFEDE5)
    static let paper = Color(hex: 0xF3F0E7)
    static let paperLight = Color(hex: 0xFAF8F2)
    static let paperDeep = Color(hex: 0xEAE5D9)
    static let ink = Color(hex: 0x23221E)
    static let inkSoft = Color(hex: 0x3B3933)
    static let muted = Color(hex: 0x6F6A5F)
    static let faint = Color(hex: 0xA9A294)
    static let rule = Color(hex: 0xD5CFC1)
    static let ruleStrong = Color(hex: 0xB9B2A2)
    static let blue = Color(hex: 0x244E6B)
    static let blueSoft = Color(hex: 0xE7EDF1)
    static let rust = Color(hex: 0x9C3F2C)
    static let ochre = Color(hex: 0x8A6420)
    static let moss = Color(hex: 0x486346)
    static let plum = Color(hex: 0x64364C)
    static let teal = Color(hex: 0x2D5F58)
    static let live = Color(hex: 0x00BF63)
    static let error = Color(hex: 0xC7483F)

    static let accents: [Color] = [rust, blue, ochre, moss, plum, teal]
}

extension Color {
    init(hex: UInt32, alpha: Double = 1) {
        self.init(
            .sRGB,
            red: Double((hex >> 16) & 0xFF) / 255,
            green: Double((hex >> 8) & 0xFF) / 255,
            blue: Double(hex & 0xFF) / 255,
            opacity: alpha
        )
    }
}

// MARK: - Type

enum HFont {
    static func display(_ size: CGFloat, weight: Font.Weight = .regular) -> Font {
        .system(size: size, weight: weight, design: .serif)
    }

    static func body(_ size: CGFloat = 13, weight: Font.Weight = .regular) -> Font {
        .system(size: size, weight: weight)
    }

    static func label(_ size: CGFloat = 10.5, weight: Font.Weight = .medium) -> Font {
        .system(size: size, weight: weight)
    }

    static func mono(_ size: CGFloat = 11) -> Font {
        .system(size: size, weight: .regular, design: .monospaced)
    }
}

// MARK: - Surfaces

struct PaperSurface: View {
    var body: some View {
        HColor.paper.ignoresSafeArea()
    }
}

struct Hairline: View {
    var color: Color = HColor.rule
    var body: some View {
        Rectangle().fill(color).frame(height: 1)
    }
}

struct VerticalHairline: View {
    var body: some View {
        Rectangle().fill(HColor.rule).frame(width: 1)
    }
}

struct CardSurface: ViewModifier {
    var padding: CGFloat = 18
    var background: Color = HColor.paperLight

    func body(content: Content) -> some View {
        content
            .padding(padding)
            .background(background)
            .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 10, style: .continuous)
                    .stroke(HColor.rule, lineWidth: 1)
            )
    }
}

extension View {
    func cardSurface(padding: CGFloat = 18, background: Color = HColor.paperLight) -> some View {
        modifier(CardSurface(padding: padding, background: background))
    }

    func sectionRule() -> some View {
        overlay(alignment: .top) {
            Hairline().offset(y: -1)
        }
    }
}

// MARK: - Labels

struct EyebrowLabel: View {
    let text: String
    var color: Color = HColor.muted

    var body: some View {
        Text(text)
            .font(HFont.label(10.5, weight: .medium))
            .tracking(0.5)
            .foregroundStyle(color)
    }
}

struct SectionTitle: View {
    let kicker: String?
    let title: String
    let subtitle: String?

    init(_ title: String, kicker: String? = nil, subtitle: String? = nil) {
        self.title = title
        self.kicker = kicker
        self.subtitle = subtitle
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            if let kicker {
                EyebrowLabel(text: kicker)
            }
            Text(title)
                .font(HFont.display(38))
                .foregroundStyle(HColor.ink)
                .fixedSize(horizontal: false, vertical: true)
            if let subtitle {
                Text(subtitle)
                    .font(HFont.body(13.5))
                    .foregroundStyle(HColor.muted)
                    .lineSpacing(3)
                    .frame(maxWidth: 650, alignment: .leading)
            }
        }
    }
}

struct StatusDot: View {
    var color: Color = HColor.live
    var size: CGFloat = 7

    var body: some View {
        Circle()
            .fill(color)
            .frame(width: size, height: size)
            .overlay(Circle().stroke(color.opacity(0.25), lineWidth: 4))
    }
}

struct Tag: View {
    let text: String
    var color: Color = HColor.muted
    var fill: Color = .clear

    var body: some View {
        Text(text)
            .font(HFont.label(9.5, weight: .semibold))
            .tracking(0.1)
            .foregroundStyle(color)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(fill)
            .overlay(
                Capsule().stroke(color.opacity(fill == .clear ? 0.35 : 0), lineWidth: 1)
            )
            .clipShape(Capsule())
    }
}

struct KeyValue: View {
    let key: String
    let value: String

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 12) {
            Text(key)
                .font(HFont.body(10.5))
                .foregroundStyle(HColor.muted)
                .frame(width: 92, alignment: .leading)
            Text(value)
                .font(HFont.body(12.5))
                .foregroundStyle(HColor.inkSoft)
                .textSelection(.enabled)
            Spacer(minLength: 0)
        }
    }
}

// MARK: - Buttons

struct PrimaryButtonStyle: ButtonStyle {
    var tint: Color = HColor.ink
    var textColor: Color = HColor.paperLight
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(HFont.body(12.5, weight: .medium))
            .foregroundStyle(textColor)
            .padding(.horizontal, 16)
            .padding(.vertical, 9)
            .background(tint.opacity(configuration.isPressed ? 0.82 : 1))
            .clipShape(RoundedRectangle(cornerRadius: 7, style: .continuous))
            .opacity(isEnabled ? 1 : 0.42)
    }
}

struct SecondaryButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(HFont.body(12.5, weight: .medium))
            .foregroundStyle(HColor.ink)
            .padding(.horizontal, 14)
            .padding(.vertical, 8)
            .background(HColor.paperLight.opacity(configuration.isPressed ? 0.6 : 0.01))
            .overlay(
                RoundedRectangle(cornerRadius: 7, style: .continuous)
                    .stroke(HColor.ruleStrong, lineWidth: 1)
            )
            .clipShape(RoundedRectangle(cornerRadius: 7, style: .continuous))
            .opacity(isEnabled ? 1 : 0.42)
    }
}

struct QuietButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(HFont.body(12, weight: .medium))
            .foregroundStyle(configuration.isPressed ? HColor.muted : HColor.inkSoft)
            .padding(.vertical, 4)
            .opacity(isEnabled ? 1 : 0.42)
    }
}

// MARK: - Inputs

struct SearchField: View {
    let placeholder: String
    @Binding var text: String

    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: "magnifyingglass")
                .font(.system(size: 12, weight: .medium))
                .foregroundStyle(HColor.muted)
            TextField(placeholder, text: $text)
                .textFieldStyle(.plain)
                .font(HFont.body(12.5))
                .foregroundStyle(HColor.ink)
            if !text.isEmpty {
                Button {
                    text = ""
                } label: {
                    Image(systemName: "xmark.circle.fill")
                        .font(.system(size: 12))
                        .foregroundStyle(HColor.faint)
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, 11)
        .padding(.vertical, 8)
        .background(HColor.paperLight)
        .overlay(
            RoundedRectangle(cornerRadius: 7, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 7, style: .continuous))
    }
}

struct FilledTagField: View {
    let label: String
    @Binding var text: String
    var placeholder: String = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            EyebrowLabel(text: label)
            TextField(placeholder, text: $text)
                .textFieldStyle(.plain)
                .font(HFont.body(12.5))
                .foregroundStyle(HColor.ink)
                .padding(.horizontal, 10)
                .padding(.vertical, 8)
                .background(HColor.paperLight)
                .overlay(
                    RoundedRectangle(cornerRadius: 6, style: .continuous)
                        .stroke(HColor.rule, lineWidth: 1)
                )
                .clipShape(RoundedRectangle(cornerRadius: 6, style: .continuous))
        }
    }
}

// MARK: - Progress

struct HProgressBar: View {
    var value: Double
    var tint: Color = HColor.ink
    var height: CGFloat = 4

    var body: some View {
        GeometryReader { proxy in
            ZStack(alignment: .leading) {
                Capsule().fill(HColor.rule.opacity(0.55))
                Capsule()
                    .fill(tint)
                    .frame(width: max(0, min(1, value)) * proxy.size.width)
            }
        }
        .frame(height: height)
    }
}

// MARK: - Cover art

struct CoverArt: View {
    let title: String
    let artist: String
    var artworkPath: String?
    var size: CGFloat = 64
    var corner: CGFloat = 4

    private var seedColor: Color {
        let hash = title.unicodeScalars.reduce(UInt32(2_166_136_261)) { value, scalar in
            (value ^ UInt32(scalar.value)) &* 16_777_619
        }
        return HColor.accents[Int(hash % UInt32(HColor.accents.count))]
    }

    var body: some View {
        Group {
            if let artworkPath, let image = NSImage(contentsOfFile: artworkPath) {
                Image(nsImage: image)
                    .resizable()
                    .aspectRatio(contentMode: .fill)
            } else {
                ZStack {
                    HColor.paperDeep
                    GeometryReader { proxy in
                        let w = proxy.size.width
                        let h = proxy.size.height
                        Canvas { context, _ in
                            var path = Path()
                            path.move(to: CGPoint(x: 0, y: h * 0.72))
                            path.addQuadCurve(
                                to: CGPoint(x: w, y: h * 0.42),
                                control: CGPoint(x: w * 0.45, y: h * 0.25)
                            )
                            path.addLine(to: CGPoint(x: w, y: h))
                            path.addLine(to: CGPoint(x: 0, y: h))
                            path.closeSubpath()
                            context.fill(path, with: .color(seedColor.opacity(0.22)))
                            context.stroke(
                                Path { p in
                                    p.move(to: CGPoint(x: w * 0.12, y: h * 0.30))
                                    p.addLine(to: CGPoint(x: w * 0.88, y: h * 0.30))
                                    p.move(to: CGPoint(x: w * 0.12, y: h * 0.34))
                                    p.addLine(to: CGPoint(x: w * 0.64, y: h * 0.34))
                                },
                                with: .color(seedColor.opacity(0.55)),
                                lineWidth: 1
                            )
                            context.fill(
                                Path(ellipseIn: CGRect(x: w * 0.68, y: h * 0.12, width: w * 0.14, height: w * 0.14)),
                                with: .color(seedColor.opacity(0.75))
                            )
                        }
                    }
                }
            }
        }
        .frame(width: size, height: size)
        .clipShape(RoundedRectangle(cornerRadius: corner, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: corner, style: .continuous)
                .stroke(HColor.ink.opacity(0.10), lineWidth: 1)
        )
    }
}

// MARK: - Empty state

struct EmptyPanel: View {
    let icon: String
    let title: String
    let message: String
    var actionTitle: String?
    var action: (() -> Void)?

    var body: some View {
        VStack(spacing: 14) {
            Image(systemName: icon)
                .font(.system(size: 24, weight: .light))
                .foregroundStyle(HColor.faint)
            Text(title)
                .font(HFont.display(22))
                .foregroundStyle(HColor.ink)
            Text(message)
                .font(HFont.body(12.5))
                .foregroundStyle(HColor.muted)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 420)
            if let actionTitle, let action {
                Button(actionTitle, action: action)
                    .buttonStyle(SecondaryButtonStyle())
            }
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 44)
        .background(HColor.paperLight.opacity(0.45))
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .strokeBorder(HColor.rule, style: StrokeStyle(lineWidth: 1, dash: [4, 4]))
        )
    }
}

// MARK: - Navigation

enum CompanionSection: String, CaseIterable, Identifiable {
    case overview
    case library
    case transfer
    case metadata
    case podcasts
    case audiobooks
    case scrobbler
    case install
    case settings

    var id: String { rawValue }

    var title: String {
        switch self {
        case .overview: return "Overview"
        case .library: return "Library"
        case .transfer: return "Transfer"
        case .metadata: return "Metadata"
        case .podcasts: return "Podcasts"
        case .audiobooks: return "Audiobooks"
        case .scrobbler: return "Scrobbler"
        case .install: return "Install"
        case .settings: return "Settings"
        }
    }

    var icon: String {
        switch self {
        case .overview: return "circle.grid.2x2"
        case .library: return "music.note.list"
        case .transfer: return "arrow.up.arrow.down"
        case .metadata: return "tag"
        case .podcasts: return "dot.radiowaves.left.and.right"
        case .audiobooks: return "book.closed"
        case .scrobbler: return "waveform.path.ecg"
        case .install: return "arrow.down.circle"
        case .settings: return "slider.horizontal.3"
        }
    }

    var primaryNavigation: Bool { self != .settings }
}
