import AppKit
import SwiftUI

struct SplashView: View {
    @EnvironmentObject private var model: AppModel

    private var banner: NSImage? {
        guard let url = Bundle.module.url(forResource: "helikon-os-banner", withExtension: "png") else {
            return nil
        }
        return NSImage(contentsOf: url)
    }

    var body: some View {
        ZStack {
            HColor.posterPaper.ignoresSafeArea()

            VStack(spacing: 0) {
                Spacer(minLength: 18)

                Group {
                    if let banner {
                        Image(nsImage: banner)
                            .resizable()
                            .interpolation(.high)
                            .scaledToFit()
                            .accessibilityLabel("Helikon-OS banner")
                    } else {
                        Rectangle()
                            .fill(HColor.paperDeep)
                            .overlay(
                                Text("Helikon-OS")
                                    .font(HFont.display(28, weight: .semibold))
                                    .foregroundStyle(HColor.muted)
                            )
                    }
                }
                .frame(maxWidth: 820, maxHeight: 420)
                .clipped()

                VStack(spacing: 9) {
                    Text("For Helikon-OS")
                        .font(HFont.label(10.5, weight: .medium))
                        .tracking(0.5)
                        .foregroundStyle(HColor.muted)

                    Text("Helikon Companion")
                        .font(HFont.display(32))
                        .foregroundStyle(HColor.ink)

                    Text("A quiet desk for the library on your iPod.")
                        .font(HFont.body(13))
                        .foregroundStyle(HColor.muted)

                    HStack(spacing: 10) {
                        Button("Open companion") {
                            model.dismissSplash()
                        }
                        .buttonStyle(PrimaryButtonStyle())
                        .keyboardShortcut(.defaultAction)

                        Button("Add music…") {
                            model.chooseAudioFiles()
                            model.dismissSplash()
                        }
                        .buttonStyle(SecondaryButtonStyle())
                    }
                    .padding(.top, 8)
                }
                .padding(.top, 26)

                Spacer(minLength: 16)

                Text("Pre-release \(AppModel.version) · The installer is not available in this build.")
                    .font(HFont.body(9.5))
                    .foregroundStyle(HColor.faint)
                    .padding(.bottom, 2)
            }
            .padding(.horizontal, 36)
            .padding(.vertical, 26)
        }
        .onExitCommand {
            model.dismissSplash()
        }
    }
}
