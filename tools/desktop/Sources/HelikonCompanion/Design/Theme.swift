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
}

// MARK: - Surfaces

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

// MARK: - Mark

struct MountainMark: View {
    var color: Color = HColor.ink

    var body: some View {
        Canvas { context, size in
            let w = size.width
            let h = size.height
            var mountains = Path()
            mountains.move(to: CGPoint(x: 0, y: h))
            mountains.addLine(to: CGPoint(x: w * 0.28, y: h * 0.38))
            mountains.addLine(to: CGPoint(x: w * 0.43, y: h * 0.68))
            mountains.addLine(to: CGPoint(x: w * 0.63, y: h * 0.18))
            mountains.addLine(to: CGPoint(x: w, y: h))
            context.stroke(mountains, with: .color(color), lineWidth: 1.3)
            let sun = Path(ellipseIn: CGRect(x: w * 0.68, y: h * 0.04, width: h * 0.22, height: h * 0.22))
            context.fill(sun, with: .color(color))
        }
    }
}
