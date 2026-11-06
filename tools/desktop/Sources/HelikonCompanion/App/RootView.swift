import SwiftUI

struct RootView: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        Group {
            if model.isShowingSplash {
                SplashView()
                    .transition(.opacity)
            } else {
                CompanionShell()
                    .transition(.opacity)
            }
        }
        .background(HColor.paper)
        .animation(.easeInOut(duration: 0.28), value: model.isShowingSplash)
    }
}

private struct CompanionShell: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        HStack(spacing: 0) {
            SidebarShell()
                .frame(width: 248)
            VerticalHairline()
            VStack(alignment: .leading, spacing: 20) {
                EyebrowLabel(text: "Helikon-OS companion")
                Text("A clean slate.")
                    .font(HFont.display(38))
                    .foregroundStyle(HColor.ink)
                Text("The previous feature set has been removed. The library, transfer, metadata, podcast, audiobook, scrobbler and installer surfaces are gone; this shell is ready for the capabilities Helikon-OS actually has.")
                    .font(HFont.body(13.5))
                    .foregroundStyle(HColor.muted)
                    .lineSpacing(3)
                    .frame(maxWidth: 620, alignment: .leading)
                Hairline()
                    .frame(maxWidth: 620)
                Text("No features are wired in this build.")
                    .font(HFont.body(11))
                    .foregroundStyle(HColor.faint)
                Button("Show welcome screen") {
                    model.showSplash()
                }
                .buttonStyle(SecondaryButtonStyle())
            }
            .padding(.horizontal, 36)
            .padding(.top, 38)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            .background(HColor.paper)
        }
        .background(HColor.paper)
    }
}

private struct SidebarShell: View {
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            VStack(alignment: .leading, spacing: 6) {
                HStack(spacing: 10) {
                    MountainMark()
                        .frame(width: 30, height: 19)
                    Text("Helikon")
                        .font(HFont.display(23, weight: .semibold))
                        .tracking(0.2)
                        .foregroundStyle(HColor.ink)
                }
                Text("Companion")
                    .font(HFont.display(12))
                    .tracking(0.7)
                    .foregroundStyle(HColor.muted)
                    .padding(.leading, 40)
            }
            .padding(.horizontal, 22)
            .padding(.top, 38)

            Spacer()

            VStack(alignment: .leading, spacing: 7) {
                Hairline()
                    .padding(.bottom, 5)
                Text("Pre-release · \(AppModel.version)")
                    .font(HFont.body(9.5, weight: .medium))
                    .foregroundStyle(HColor.muted)
                Text("No feature modules are present in this build.")
                    .font(HFont.body(9.5))
                    .foregroundStyle(HColor.faint)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .padding(.horizontal, 22)
            .padding(.bottom, 20)
        }
        .background(HColor.paperDeep)
        .overlay(alignment: .trailing) {
            VerticalHairline()
        }
    }
}
