import AppKit
import CompanionCore
import SwiftUI

struct SettingsView: View {
    @EnvironmentObject private var model: AppModel
    @State private var secret = ""
    @State private var showingSecret = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 24) {
                SectionTitle(
                    "Settings",
                    kicker: "Preferences",
                    subtitle: "Network, providers, transfer policy and the Last.fm connection. The defaults keep automatic work off; every search, download and scrobble starts with a press."
                )
                general
                transfer
                lastFM
                privacy
                about
            }
            .padding(.horizontal, 32)
            .padding(.vertical, 30)
        }
        .background(HColor.paper)
    }

    private var general: some View {
        SettingsCard(title: "General", note: "The app has no account and no telemetry. Metadata lookups are the only network feature enabled by default.") {
            SettingsToggle(
                title: "Allow metadata lookups",
                detail: "MusicBrainz and Apple are contacted only when you press Search.",
                isOn: $model.settings.networkEnabled
            )
            Hairline()
            VStack(alignment: .leading, spacing: 9) {
                EyebrowLabel(text: "Metadata providers")
                ForEach([MetadataProvider.musicBrainz, .iTunes], id: \.self) { provider in
                    Toggle(isOn: Binding(
                        get: { model.settings.metadataProviders.contains(provider) },
                        set: { enabled in
                            if enabled {
                                model.settings.metadataProviders.insert(provider)
                            } else {
                                model.settings.metadataProviders.remove(provider)
                            }
                        }
                    )) {
                        Text(provider.title)
                            .font(HFont.body(11.5))
                            .foregroundStyle(HColor.inkSoft)
                    }
                    .toggleStyle(.checkbox)
                    .font(HFont.body(11.5))
                }
            }
            Hairline()
            HStack(spacing: 10) {
                Button("Load sample library") {
                    model.loadDemoLibrary()
                }
                .buttonStyle(SecondaryButtonStyle())
                .disabled(model.demoMode)
                Button("Clear sample library") {
                    model.clearDemoLibrary()
                }
                .buttonStyle(SecondaryButtonStyle())
                .disabled(!model.demoMode)
            }
        }
    }

    private var transfer: some View {
        SettingsCard(title: "Transfer", note: "The planner writes only inside the chosen folder. A replacement is backed up and checked before the destination changes.") {
            HStack(spacing: 12) {
                FilledTagField(label: "Music folder", text: $model.settings.defaultMusicFolder, placeholder: "Music/Helikon Companion")
                FilledTagField(label: "Podcast folder", text: $model.settings.defaultPodcastFolder, placeholder: "Podcasts")
                FilledTagField(label: "Audiobook folder", text: $model.settings.defaultAudiobookFolder, placeholder: "Audiobooks")
            }
            Hairline()
            HStack(spacing: 20) {
                VStack(alignment: .leading, spacing: 6) {
                    EyebrowLabel(text: "When a file exists")
                    Picker("", selection: $model.settings.conflictPolicy) {
                        ForEach(TransferConflictPolicy.allCases, id: \.self) { policy in
                            Text(policy.title).tag(policy)
                        }
                    }
                    .labelsHidden()
                    .frame(width: 210)
                }
                Spacer()
                VStack(alignment: .leading, spacing: 7) {
                    Toggle("Verify every copy with SHA-256", isOn: $model.settings.verifyTransfers)
                        .toggleStyle(.checkbox)
                    Toggle("Write edited tags into MP3 copies", isOn: $model.settings.writeMP3TagsOnTransfer)
                        .toggleStyle(.checkbox)
                    Text("A replaced destination is always backed up and verified first.")
                        .font(HFont.body(9.5))
                        .foregroundStyle(HColor.faint)
                }
                .font(HFont.body(11))
            }
            Hairline()
            HStack(spacing: 8) {
                Button("Reveal destination") {
                    model.revealDestination(for: .music)
                }
                .buttonStyle(SecondaryButtonStyle())
                Button("Open backup folder") {
                    NSWorkspace.shared.open(AppPaths.backupsDirectory)
                }
                .buttonStyle(SecondaryButtonStyle())
                Spacer()
                Text("Only MP3 tag writing is implemented; other formats keep their companion metadata.")
                    .font(HFont.body(9.5))
                    .foregroundStyle(HColor.faint)
            }
        }
    }

    private var lastFM: some View {
        SettingsCard(title: "Last.fm", note: "API keys are not shipped with the app. Create a Last.fm API account, paste the key and secret here, then complete the browser approval.") {
            HStack(spacing: 12) {
                FilledTagField(label: "API key", text: $model.settings.lastFMAPIKey, placeholder: "32-character API key")
                VStack(alignment: .leading, spacing: 6) {
                    EyebrowLabel(text: "API secret")
                    HStack(spacing: 8) {
                        Group {
                            if showingSecret {
                                TextField("Secret", text: $secret)
                            } else {
                                SecureField("Secret", text: $secret)
                            }
                        }
                        .textFieldStyle(.plain)
                        .font(HFont.body(11.5))
                        .padding(.horizontal, 10)
                        .padding(.vertical, 8)
                        .background(HColor.paperLight)
                        .overlay(
                            RoundedRectangle(cornerRadius: 6, style: .continuous)
                                .stroke(HColor.rule, lineWidth: 1)
                        )
                        .clipShape(RoundedRectangle(cornerRadius: 6, style: .continuous))
                        Button {
                            showingSecret.toggle()
                        } label: {
                            Image(systemName: showingSecret ? "eye.slash" : "eye")
                                .font(.system(size: 10))
                                .foregroundStyle(HColor.muted)
                        }
                        .buttonStyle(.plain)
                        Button("Save in Keychain") {
                            model.saveLastFMSecret(secret)
                            secret = ""
                        }
                        .buttonStyle(SecondaryButtonStyle())
                    }
                    Text(model.lastFMSecretIsStored ? "Secret stored in the macOS Keychain." : "No secret stored.")
                        .font(HFont.body(9.5))
                        .foregroundStyle(model.lastFMSecretIsStored ? HColor.moss : HColor.faint)
                }
            }
            Hairline()
            HStack(spacing: 9) {
                if model.lastFMSessionIsStored {
                    StatusDot(color: HColor.live, size: 6)
                    Text("Connected as \(model.settings.lastFMUsername)")
                        .font(HFont.body(11))
                        .foregroundStyle(HColor.inkSoft)
                    Spacer()
                    Button("Disconnect") { model.disconnectLastFM() }
                        .buttonStyle(SecondaryButtonStyle())
                } else {
                    StatusDot(color: HColor.faint, size: 6)
                    Text("Not connected")
                        .font(HFont.body(11))
                        .foregroundStyle(HColor.muted)
                    Spacer()
                    Button("Begin connection") {
                        Task { await model.beginLastFMAuthentication() }
                    }
                    .buttonStyle(PrimaryButtonStyle())
                    .disabled(!model.lastFMSecretIsStored || model.settings.lastFMAPIKey.isEmpty)
                    if let url = model.lastFMAuthURL {
                        Button("Open Last.fm") { NSWorkspace.shared.open(url) }
                            .buttonStyle(SecondaryButtonStyle())
                        Button("Complete") {
                            Task { await model.completeLastFMAuthentication() }
                        }
                        .buttonStyle(PrimaryButtonStyle())
                    }
                }
            }
            if !model.lastFMStatus.isEmpty {
                Text(model.lastFMStatus)
                    .font(HFont.body(10))
                    .foregroundStyle(HColor.muted)
            }
        }
    }

    private var privacy: some View {
        SettingsCard(title: "Privacy and safety", note: "The device has no network stack. No listening history leaves this Mac until the Last.fm action is explicit.") {
            Text("The app reads mounted HFS+ metadata and audio tags. It writes only to a destination you choose, plus its own Application Support folder for the library, artwork cache, downloads and verified backups. Scrobbling is off until a Last.fm session is connected. Podcast and audiobook downloads happen on the Mac. There is no telemetry and no hidden upload.")
                .font(HFont.body(10.5))
                .foregroundStyle(HColor.muted)
                .lineSpacing(3)
            Hairline()
            HStack(spacing: 9) {
                Button("Open Application Support") {
                    NSWorkspace.shared.open(AppPaths.supportDirectory)
                }
                .buttonStyle(SecondaryButtonStyle())
                Button("Open backups") {
                    NSWorkspace.shared.open(AppPaths.backupsDirectory)
                }
                .buttonStyle(SecondaryButtonStyle())
                Spacer()
                Text("No credentials are stored outside the Keychain.")
                    .font(HFont.body(9.5))
                    .foregroundStyle(HColor.faint)
            }
        }
    }

    private var about: some View {
        HStack(alignment: .top, spacing: 18) {
            MountainMark()
                .frame(width: 38, height: 24)
            VStack(alignment: .leading, spacing: 5) {
                Text("Helikon Companion \(AppModel.version)")
                    .font(HFont.display(20))
                    .foregroundStyle(HColor.ink)
                Text("A macOS-first companion for Helikon-OS and native HFS+ iPods. Pre-release; the installer and firmware handoff are not available.")
                    .font(HFont.body(10.5))
                    .foregroundStyle(HColor.muted)
                    .lineSpacing(2)
            }
            Spacer()
            HStack(spacing: 8) {
                Button("Welcome screen") {
                    model.showSplash()
                }
                .buttonStyle(SecondaryButtonStyle())
                Button("Project repository") {
                    if let url = URL(string: "https://github.com/deandromi/Helikon-OS") {
                        NSWorkspace.shared.open(url)
                    }
                }
                .buttonStyle(SecondaryButtonStyle())
            }
        }
        .padding(.top, 4)
        .padding(.bottom, 18)
    }
}

private struct SettingsCard<Content: View>: View {
    let title: String
    let note: String
    let content: Content

    init(title: String, note: String, @ViewBuilder content: () -> Content) {
        self.title = title
        self.note = note
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 13) {
            HStack(alignment: .firstTextBaseline) {
                Text(title)
                    .font(HFont.display(21))
                    .foregroundStyle(HColor.ink)
                Spacer()
            }
            Text(note)
                .font(HFont.body(10.5))
                .foregroundStyle(HColor.muted)
                .lineSpacing(2)
                .padding(.bottom, 2)
            content
        }
        .padding(18)
        .background(HColor.paperLight)
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
    }
}

private struct SettingsToggle: View {
    let title: String
    let detail: String
    @Binding var isOn: Bool

    var body: some View {
        Toggle(isOn: $isOn) {
            VStack(alignment: .leading, spacing: 3) {
                Text(title)
                    .font(HFont.body(11.5, weight: .medium))
                    .foregroundStyle(HColor.ink)
                Text(detail)
                    .font(HFont.body(10))
                    .foregroundStyle(HColor.muted)
            }
        }
        .toggleStyle(.switch)
        .controlSize(.small)
    }
}
