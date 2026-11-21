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
        VStack(alignment: .leading, spacing: 0) {
            header

            VStack(alignment: .leading, spacing: 16) {
                Text("A clean slate.")
                    .font(HFont.display(34))
                    .foregroundStyle(HColor.ink)
                    .padding(.bottom, 2)
                Text(
                    "The previous feature set has been removed: the library, "
                        + "transfer, metadata, podcast, audiobook, scrobbler and "
                        + "installer surfaces are gone, and none of the "
                        + "replacement capabilities ship yet. This shell is "
                        + "ready for the ones Helikon-OS actually has."
                )
                .font(HFont.body(13.5))
                .foregroundStyle(HColor.muted)
                .lineSpacing(3)
                .frame(maxWidth: 560, alignment: .leading)
                Button("Show welcome screen") {
                    model.showSplash()
                }
                .buttonStyle(SecondaryButtonStyle())
                .padding(.top, 6)
            }
            .padding(.horizontal, 34)
            .padding(.top, 30)

            Spacer(minLength: 24)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(HColor.paper)
    }

    /// One band across the top instead of a full-height empty rail. The traffic
    /// lights overlay a hidden title bar, so the band starts below them.
    private var header: some View {
        VStack(spacing: 0) {
            HStack(alignment: .center, spacing: 10) {
                MountainMark()
                    .frame(width: 26, height: 16)
                Text("Helikon")
                    .font(HFont.display(19, weight: .semibold))
                    .tracking(0.2)
                    .foregroundStyle(HColor.ink)
                Text("Companion")
                    .font(HFont.body(11))
                    .foregroundStyle(HColor.muted)
                Spacer(minLength: 12)
                EyebrowLabel(text: "PRE-RELEASE \(AppModel.version)")
            }
            .padding(.horizontal, 20)
            .padding(.top, 34)
            .padding(.bottom, 12)

            Hairline()
        }
        .background(HColor.paperDeep)
    }
}
