import AppKit
import CompanionCore
import SwiftUI

struct ScrobblerView: View {
    @EnvironmentObject private var model: AppModel

    private var connected: Bool {
        model.settings.scrobblingEnabled && !model.settings.lastFMUsername.isEmpty && model.lastFMSessionIsStored
    }

    private var queued: Int { model.snapshot.scrobbles.filter { $0.status == .queued }.count }
    private var failed: Int { model.snapshot.scrobbles.filter { $0.status == .failed }.count }
    private var sent: Int { model.snapshot.scrobbles.filter { $0.status == .sent }.count }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 24) {
                SectionTitle(
                    "Scrobbler",
                    kicker: "Local history",
                    subtitle: "Listening history stays local until you connect Last.fm and press send. The device has no network and never talks to Last.fm itself."
                )
                connectionCard
                stats
                queue
                privacyNote
            }
            .padding(.horizontal, 32)
            .padding(.vertical, 30)
        }
        .background(HColor.paper)
    }

    private var connectionCard: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(alignment: .top) {
                HStack(spacing: 10) {
                    StatusDot(color: connected ? HColor.live : HColor.faint, size: 7)
                    VStack(alignment: .leading, spacing: 3) {
                        Text(connected ? "Last.fm connected" : "Last.fm not connected")
                            .font(HFont.display(20))
                            .foregroundStyle(HColor.ink)
                        Text(connected ? model.settings.lastFMUsername : "Opt-in only. Nothing is sent automatically.")
                            .font(HFont.body(10.5))
                            .foregroundStyle(HColor.muted)
                    }
                }
                Spacer()
                Toggle("Scrobble plays", isOn: $model.settings.scrobblingEnabled)
                    .toggleStyle(.switch)
                    .controlSize(.small)
                    .font(HFont.body(11))
                    .disabled(!model.lastFMSessionIsStored)
            }

            if model.lastFMSessionIsStored {
                HStack(spacing: 8) {
                    Button("Disconnect") {
                        model.disconnectLastFM()
                    }
                    .buttonStyle(SecondaryButtonStyle())
                    if !model.lastFMStatus.isEmpty {
                        Text(model.lastFMStatus)
                            .font(HFont.body(10))
                            .foregroundStyle(HColor.muted)
                    }
                }
            } else {
                VStack(alignment: .leading, spacing: 10) {
                    Text("Last.fm needs an API key and secret. They are kept in Settings; the secret goes to the macOS Keychain, never into the repository.")
                        .font(HFont.body(10.5))
                        .foregroundStyle(HColor.muted)
                        .lineSpacing(2)
                    HStack(spacing: 8) {
                        Button("Open Last.fm settings") {
                            model.section = .settings
                        }
                        .buttonStyle(SecondaryButtonStyle())
                        if model.lastFMSecretIsStored && !model.settings.lastFMAPIKey.isEmpty {
                            Button("Begin connection") {
                                Task { await model.beginLastFMAuthentication() }
                            }
                            .buttonStyle(PrimaryButtonStyle())
                        }
                        if let url = model.lastFMAuthURL {
                            Button("Open Last.fm") {
                                NSWorkspace.shared.open(url)
                            }
                            .buttonStyle(SecondaryButtonStyle())
                            Button("Complete connection") {
                                Task { await model.completeLastFMAuthentication() }
                            }
                            .buttonStyle(PrimaryButtonStyle())
                        }
                    }
                    if !model.lastFMStatus.isEmpty {
                        Text(model.lastFMStatus)
                            .font(HFont.body(10))
                            .foregroundStyle(HColor.muted)
                    }
                }
            }
        }
        .padding(18)
        .background(HColor.paperLight)
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private var stats: some View {
        HStack(spacing: 0) {
            ScrobbleStat(value: "\(queued)", label: "Queued", tint: HColor.ochre)
            ScrobbleDivider()
            ScrobbleStat(value: "\(failed)", label: "Failed", tint: HColor.error)
            ScrobbleDivider()
            ScrobbleStat(value: "\(sent)", label: "Sent", tint: HColor.moss)
            Spacer()
            HStack(spacing: 8) {
                Button("Import history") { model.importScrobbles() }
                    .buttonStyle(SecondaryButtonStyle())
                Button("Read from device") { model.importDevicePlayHistory() }
                    .buttonStyle(SecondaryButtonStyle())
                Button {
                    Task { await model.flushScrobbles() }
                } label: {
                    Label("Send queue", systemImage: "paperplane")
                }
                .buttonStyle(PrimaryButtonStyle())
                .disabled(queued + failed == 0)
            }
            .padding(.horizontal, 20)
        }
        .padding(.vertical, 4)
        .background(HColor.paperLight.opacity(0.45))
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private var queue: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("Play history")
                    .font(HFont.display(21))
                    .foregroundStyle(HColor.ink)
                Spacer()
                Text("\(model.snapshot.scrobbles.count) rows")
                    .font(HFont.mono(9.5))
                    .foregroundStyle(HColor.muted)
            }
            .padding(.bottom, 11)
            if model.snapshot.scrobbles.isEmpty {
                EmptyPanel(
                    icon: "waveform.path.ecg",
                    title: "No plays queued",
                    message: "Queue a track from the Library, or import a Rockbox .scrobbler.log / JSON play history."
                )
            } else {
                ForEach(Array(model.snapshot.scrobbles.sorted(by: { $0.timestamp > $1.timestamp }).prefix(40))) { entry in
                    HStack(spacing: 12) {
                        Circle()
                            .fill(statusColor(entry.status))
                            .frame(width: 5, height: 5)
                        VStack(alignment: .leading, spacing: 3) {
                            Text(entry.title)
                                .font(HFont.body(11.5, weight: .medium))
                                .foregroundStyle(HColor.ink)
                            Text([entry.artist, entry.album].filter { !$0.isEmpty }.joined(separator: " · "))
                                .font(HFont.body(10))
                                .foregroundStyle(HColor.muted)
                        }
                        Spacer()
                        Text(Formatters.time(entry.timestamp))
                            .font(HFont.mono(9))
                            .foregroundStyle(HColor.faint)
                        Tag(text: entry.status.rawValue.capitalized, color: statusColor(entry.status))
                        if let error = entry.error {
                            Image(systemName: "exclamationmark.circle")
                                .font(.system(size: 10))
                                .foregroundStyle(HColor.error)
                                .help(error)
                        }
                    }
                    .padding(.vertical, 9)
                    Hairline()
                }
            }
        }
        .padding(18)
        .background(HColor.paperLight)
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private var privacyNote: some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: "lock")
                .font(.system(size: 12, weight: .medium))
                .foregroundStyle(HColor.muted)
            Text("Offline-first. The queue is stored in the companion library and sent only when you press Send queue. Revoking the session or turning scrobbling off stops all network calls. The app never reads a Last.fm password and never puts an API secret in the repository.")
                .font(HFont.body(10))
                .foregroundStyle(HColor.muted)
                .lineSpacing(2)
            Spacer()
        }
        .padding(14)
        .background(HColor.paperDeep.opacity(0.4))
        .overlay(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
    }

    private func statusColor(_ status: ScrobbleStatus) -> Color {
        switch status {
        case .queued: return HColor.ochre
        case .sending: return HColor.blue
        case .sent: return HColor.moss
        case .failed: return HColor.error
        }
    }
}

private struct ScrobbleStat: View {
    let value: String
    let label: String
    let tint: Color

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(value)
                .font(HFont.display(26))
                .foregroundStyle(HColor.ink)
                .monospacedDigit()
            HStack(spacing: 5) {
                Circle().fill(tint).frame(width: 5, height: 5)
                Text(label)
                    .font(HFont.body(10))
                    .foregroundStyle(HColor.muted)
            }
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 12)
    }
}

private struct ScrobbleDivider: View {
    var body: some View {
        Rectangle().fill(HColor.rule).frame(width: 1, height: 38)
    }
}
