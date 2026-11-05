import SwiftUI
import CompanionCore

struct DashboardView: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 26) {
                header
                if model.demoMode { demoBanner }
                if let device = model.selectedDevice {
                    deviceCard(device)
                } else {
                    connectCard
                }
                stats
                HStack(alignment: .top, spacing: 20) {
                    attentionCard
                    activityCard
                        .frame(width: 320)
                }
                albumShelf
            }
            .padding(.horizontal, 32)
            .padding(.vertical, 30)
        }
        .background(HColor.paper)
    }

    private var header: some View {
        HStack(alignment: .top, spacing: 20) {
            SectionTitle(
                "Overview",
                kicker: "The companion folio",
                subtitle: "A quiet desk for the library on your iPod: read it, tag it, move it, and keep a record of what leaves the house."
            )
            Spacer(minLength: 20)
            VStack(alignment: .trailing, spacing: 8) {
                Button {
                    model.chooseAudioFiles()
                } label: {
                    Label("Add music", systemImage: "plus")
                }
                .buttonStyle(PrimaryButtonStyle())
                HStack(spacing: 14) {
                    if model.selectedDevice != nil {
                        Button("Scan volume") {
                            model.scanSelectedDevice()
                        }
                        .buttonStyle(QuietButtonStyle())
                    }
                    Button("Load sample library") {
                        model.loadDemoLibrary()
                    }
                    .buttonStyle(QuietButtonStyle())
                    .disabled(model.demoMode)
                }
            }
            .padding(.top, 4)
        }
    }

    private var demoBanner: some View {
        HStack(spacing: 12) {
            Image(systemName: "sparkles")
                .font(.system(size: 13, weight: .medium))
                .foregroundStyle(HColor.ochre)
            VStack(alignment: .leading, spacing: 2) {
                Text("Sample library")
                    .font(HFont.body(12, weight: .semibold))
                    .foregroundStyle(HColor.ink)
                Text("These records are for preview only. They have no files and cannot be transferred.")
                    .font(HFont.body(11))
                    .foregroundStyle(HColor.muted)
            }
            Spacer()
            Button("Clear sample") {
                model.clearDemoLibrary()
            }
            .buttonStyle(SecondaryButtonStyle())
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 11)
        .background(HColor.paperLight)
        .overlay(alignment: .leading) {
            Rectangle().fill(HColor.ochre).frame(width: 2)
        }
        .overlay(
            RoundedRectangle(cornerRadius: 7, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 7, style: .continuous))
    }

    private func deviceCard(_ device: DeviceInfo) -> some View {
        HStack(alignment: .center, spacing: 26) {
            VStack(alignment: .leading, spacing: 9) {
                HStack(spacing: 8) {
                    EyebrowLabel(text: "Connected volume")
                    Tag(text: device.kindLabel, color: HColor.moss)
                }
                Text(device.name)
                    .font(HFont.display(31))
                    .foregroundStyle(HColor.ink)
                Text(device.capacityText)
                    .font(HFont.body(12.5))
                    .foregroundStyle(HColor.muted)
                HProgressBar(value: device.usedFraction, tint: HColor.inkSoft, height: 5)
                    .frame(maxWidth: 430)
                HStack(spacing: 18) {
                    DeviceFact(label: "Format", value: device.filesystem)
                    DeviceFact(label: "Bus", value: device.busProtocol ?? "—")
                    DeviceFact(label: "Access", value: device.isReadOnly ? "Read-only" : "Read + write")
                }
                .padding(.top, 3)
            }
            Spacer(minLength: 20)
            DeviceSilhouette(device: device)
                .frame(width: 130, height: 168)
        }
        .padding(24)
        .background(HColor.paperLight)
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
    }

    private var connectCard: some View {
        HStack(alignment: .center, spacing: 30) {
            VStack(alignment: .leading, spacing: 13) {
                EyebrowLabel(text: "No volume mounted")
                Text("Connect your iPod.")
                    .font(HFont.display(32))
                    .foregroundStyle(HColor.ink)
                Text("Put the iPod in Disk Mode with the native Menu + Play gesture, then mount it on this Mac. The companion app reads the volume in place and will never format or repair it.")
                    .font(HFont.body(12.5))
                    .foregroundStyle(HColor.muted)
                    .lineSpacing(3)
                    .frame(maxWidth: 520, alignment: .leading)
                HStack(spacing: 10) {
                    Button("Refresh volumes") {
                        Task { await model.refreshDevices() }
                    }
                    .buttonStyle(PrimaryButtonStyle())
                    Button("Choose a folder") {
                        model.chooseAudioFiles()
                    }
                    .buttonStyle(SecondaryButtonStyle())
                }
                HStack(spacing: 18) {
                    ConnectStep(number: "1", text: "Menu + Play")
                    ConnectStep(number: "2", text: "Mount HFS+")
                    ConnectStep(number: "3", text: "Scan library")
                }
                .padding(.top, 4)
            }
            Spacer(minLength: 0)
            DeviceSilhouette(device: nil)
                .frame(width: 150, height: 190)
                .opacity(0.86)
        }
        .padding(26)
        .background(HColor.paperLight)
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
    }

    private var stats: some View {
        HStack(spacing: 0) {
            StatCell(
                value: "\(model.trackCount)",
                label: "Tracks",
                detail: "\(model.albumCount) albums",
                tint: HColor.rust
            )
            StatDivider()
            StatCell(
                value: "\(model.albumCount)",
                label: "Albums",
                detail: "\(model.artistCount) artists",
                tint: HColor.blue
            )
            StatDivider()
            StatCell(
                value: "\(model.missingMetadataCount)",
                label: "Need metadata",
                detail: model.missingMetadataCount == 0 ? "All tagged" : "Review the queue",
                tint: HColor.ochre
            )
            StatDivider()
            StatCell(
                value: "\(model.queuedScrobbleCount)",
                label: "Scrobbles",
                detail: model.settings.scrobblingEnabled ? "Opted in" : "Off by default",
                tint: HColor.plum
            )
        }
        .padding(.vertical, 4)
        .background(HColor.paperLight.opacity(0.45))
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private var attentionCard: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("Needs attention")
                    .font(HFont.display(21))
                    .foregroundStyle(HColor.ink)
                Spacer()
                Text("\(attentionItems.count)")
                    .font(HFont.mono(10))
                    .foregroundStyle(HColor.muted)
            }
            .padding(.bottom, 10)
            ForEach(Array(attentionItems.enumerated()), id: \.offset) { index, item in
                if index > 0 { Hairline() }
                Button {
                    item.action()
                } label: {
                    HStack(spacing: 11) {
                        Circle()
                            .fill(item.tint)
                            .frame(width: 5, height: 5)
                        Text(item.title)
                            .font(HFont.body(12.5))
                            .foregroundStyle(HColor.inkSoft)
                            .multilineTextAlignment(.leading)
                        Spacer()
                        Text(item.actionTitle)
                            .font(HFont.body(10.5, weight: .medium))
                            .foregroundStyle(HColor.muted)
                        Image(systemName: "arrow.right")
                            .font(.system(size: 9, weight: .medium))
                            .foregroundStyle(HColor.faint)
                    }
                    .padding(.vertical, 11)
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
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

    private var activityCard: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("Recent activity")
                .font(HFont.display(21))
                .foregroundStyle(HColor.ink)
                .padding(.bottom, 10)
            if model.activities.isEmpty {
                Text("Nothing yet. Scan a folder or connect a volume to begin.")
                    .font(HFont.body(11.5))
                    .foregroundStyle(HColor.muted)
                    .padding(.vertical, 10)
            } else {
                ForEach(Array(model.activities.prefix(6).enumerated()), id: \.element.id) { index, entry in
                    if index > 0 { Hairline() }
                    HStack(alignment: .top, spacing: 10) {
                        Image(systemName: entry.symbol)
                            .font(.system(size: 11, weight: .medium))
                            .foregroundStyle(entry.tint)
                            .frame(width: 16)
                            .padding(.top, 2)
                        VStack(alignment: .leading, spacing: 3) {
                            Text(entry.title)
                                .font(HFont.body(11.5, weight: .medium))
                                .foregroundStyle(HColor.ink)
                            Text(entry.detail)
                                .font(HFont.body(10.5))
                                .foregroundStyle(HColor.muted)
                                .lineLimit(2)
                        }
                        Spacer(minLength: 4)
                        Text(Formatters.relativeDate(entry.date))
                            .font(HFont.body(9.5))
                            .foregroundStyle(HColor.faint)
                    }
                    .padding(.vertical, 9)
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

    private var albumShelf: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .firstTextBaseline) {
                Text("From the library")
                    .font(HFont.display(21))
                    .foregroundStyle(HColor.ink)
                Spacer()
                Button("Open library") {
                    model.section = .library
                }
                .buttonStyle(QuietButtonStyle())
            }
            if model.snapshot.albums.isEmpty {
                EmptyPanel(
                    icon: "rectangle.stack",
                    title: "No albums yet",
                    message: "Add a folder of music or load the sample library to see the shape of the app.",
                    actionTitle: "Add music",
                    action: { model.chooseAudioFiles() }
                )
            } else {
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 16), count: 5), spacing: 16) {
                    ForEach(model.snapshot.albums.prefix(5)) { album in
                        Button {
                            model.section = .library
                        } label: {
                            VStack(alignment: .leading, spacing: 8) {
                                CoverArt(
                                    title: album.title,
                                    artist: album.artist,
                                    artworkPath: album.artworkPath,
                                    size: 116,
                                    corner: 3
                                )
                                .frame(maxWidth: .infinity, alignment: .leading)
                                Text(album.title)
                                    .font(HFont.body(11.5, weight: .medium))
                                    .foregroundStyle(HColor.ink)
                                    .lineLimit(1)
                                Text(album.artist)
                                    .font(HFont.body(10.5))
                                    .foregroundStyle(HColor.muted)
                                    .lineLimit(1)
                            }
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
        }
    }

    private var attentionItems: [AttentionItem] {
        var items: [AttentionItem] = []
        if model.missingMetadataCount > 0 {
            items.append(AttentionItem(
                title: "\(model.missingMetadataCount) \(model.missingMetadataCount == 1 ? "track is" : "tracks are") missing tags or artwork",
                actionTitle: "Review",
                tint: HColor.ochre,
                action: { model.section = .metadata }
            ))
        }
        if model.demoMode {
            items.append(AttentionItem(
                title: "The sample library is active",
                actionTitle: "Clear",
                tint: HColor.ochre,
                action: { model.clearDemoLibrary() }
            ))
        }
        if !model.backupVerified {
            items.append(AttentionItem(
                title: "No verified backup in this session",
                actionTitle: "Back up",
                tint: HColor.rust,
                action: { model.backupFolder() }
            ))
        }
        if model.queuedScrobbleCount > 0 {
            items.append(AttentionItem(
                title: "\(model.queuedScrobbleCount) scrobbles are waiting for a connection",
                actionTitle: "Open",
                tint: HColor.plum,
                action: { model.section = .scrobbler }
            ))
        }
        if !model.settings.scrobblingEnabled {
            items.append(AttentionItem(
                title: "Last.fm scrobbling is off",
                actionTitle: "Set up",
                tint: HColor.plum,
                action: { model.section = .scrobbler }
            ))
        }
        if items.isEmpty {
            items.append(AttentionItem(
                title: "No urgent maintenance. The library is quiet.",
                actionTitle: "Library",
                tint: HColor.moss,
                action: { model.section = .library }
            ))
        }
        return Array(items.prefix(4))
    }
}

private struct AttentionItem {
    let title: String
    let actionTitle: String
    let tint: Color
    let action: () -> Void
}

private struct StatCell: View {
    let value: String
    let label: String
    let detail: String
    let tint: Color

    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            Text(value)
                .font(HFont.display(34))
                .foregroundStyle(HColor.ink)
                .monospacedDigit()
            HStack(spacing: 6) {
                Circle().fill(tint).frame(width: 5, height: 5)
                Text(label)
                    .font(HFont.body(10.5, weight: .medium))
                    .foregroundStyle(HColor.inkSoft)
            }
            Text(detail)
                .font(HFont.body(10))
                .foregroundStyle(HColor.muted)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 20)
        .padding(.vertical, 14)
    }
}

private struct StatDivider: View {
    var body: some View {
        Rectangle().fill(HColor.rule).frame(width: 1, height: 58)
    }
}

private struct DeviceFact: View {
    let label: String
    let value: String

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(label)
                .font(HFont.body(10))
                .foregroundStyle(HColor.muted)
            Text(value)
                .font(HFont.body(11))
                .foregroundStyle(HColor.inkSoft)
        }
    }
}

private struct ConnectStep: View {
    let number: String
    let text: String

    var body: some View {
        HStack(spacing: 7) {
            Text(number)
                .font(HFont.mono(9))
                .foregroundStyle(HColor.muted)
                .frame(width: 18, height: 18)
                .overlay(Circle().stroke(HColor.rule, lineWidth: 1))
            Text(text)
                .font(HFont.body(10))
                .foregroundStyle(HColor.muted)
        }
    }
}

struct DeviceSilhouette: View {
    let device: DeviceInfo?

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 18, style: .continuous)
                .fill(HColor.paperDeep)
                .overlay(
                    RoundedRectangle(cornerRadius: 18, style: .continuous)
                        .stroke(HColor.ruleStrong, lineWidth: 1)
                )
            VStack(spacing: 0) {
                RoundedRectangle(cornerRadius: 4, style: .continuous)
                    .fill(HColor.paperLight)
                    .overlay(
                        RoundedRectangle(cornerRadius: 4, style: .continuous)
                            .stroke(HColor.rule, lineWidth: 1)
                    )
                    .overlay(alignment: .topLeading) {
                        VStack(alignment: .leading, spacing: 3) {
                            Text("Helikon")
                                .font(HFont.display(6.5, weight: .semibold))
                                .tracking(0.2)
                                .foregroundStyle(HColor.ink)
                            Text(device == nil ? "No volume" : "HFS+")
                                .font(HFont.label(5))
                                .foregroundStyle(HColor.muted)
                        }
                        .padding(6)
                    }
                    .frame(height: 66)
                    .padding(.horizontal, 15)
                    .padding(.top, 24)

                Spacer(minLength: 10)
                ZStack {
                    Circle()
                        .stroke(HColor.ruleStrong, lineWidth: 1)
                        .frame(width: 48, height: 48)
                    Circle()
                        .fill(HColor.paperLight)
                        .frame(width: 20, height: 20)
                        .overlay(Circle().stroke(HColor.rule, lineWidth: 1))
                    VStack(spacing: 24) {
                        Text("Menu").font(HFont.label(5))
                        Text("Play").font(HFont.label(5))
                    }
                    .foregroundStyle(HColor.muted)
                    .offset(y: 1)
                }
                Spacer(minLength: 17)
            }
            .padding(.vertical, 0)
        }
    }
}
