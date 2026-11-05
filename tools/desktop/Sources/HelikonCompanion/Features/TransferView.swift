import AppKit
import CompanionCore
import SwiftUI
import UniformTypeIdentifiers

struct TransferView: View {
    @EnvironmentObject private var model: AppModel
    @State private var isDropTarget = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 24) {
                SectionTitle(
                    "Transfer",
                    kicker: "Plan before copy",
                    subtitle: "Plan first, then copy. Existing files are backed up before replacement, every copy is checked, and the app writes only inside the folder you choose."
                )
                destinationCard
                dropZone
                if let plan = model.transferPlan {
                    planCard(plan)
                } else {
                    emptyPlan
                }
                if let report = model.transferReport {
                    reportCard(report)
                }
            }
            .padding(.horizontal, 32)
            .padding(.vertical, 30)
        }
        .background(HColor.paper)
    }

    private var destinationCard: some View {
        HStack(alignment: .center, spacing: 16) {
            VStack(alignment: .leading, spacing: 5) {
                EyebrowLabel(text: "Destination")
                HStack(spacing: 8) {
                    Text(model.selectedDevice?.name ?? "Local staging")
                        .font(HFont.display(19))
                        .foregroundStyle(HColor.ink)
                    if model.selectedDevice == nil {
                        Tag(text: "No device", color: HColor.ochre)
                    } else {
                        Tag(text: "HFS+ mounted", color: HColor.moss)
                    }
                }
                Text(model.destinationLabel(for: .music))
                    .font(HFont.mono(10))
                    .foregroundStyle(HColor.muted)
                    .lineLimit(1)
                    .truncationMode(.middle)
            }
            Spacer()
            HStack(spacing: 8) {
                Button("Choose folder") {
                    model.chooseTransferDestination(for: .music)
                }
                .buttonStyle(SecondaryButtonStyle())
                Button {
                    model.revealDestination(for: .music)
                } label: {
                    Image(systemName: "folder")
                        .font(.system(size: 11))
                        .foregroundStyle(HColor.inkSoft)
                        .frame(width: 28, height: 28)
                        .overlay(Circle().stroke(HColor.rule, lineWidth: 1))
                }
                .buttonStyle(.plain)
                .help("Reveal destination")
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

    private var dropZone: some View {
        VStack(spacing: 13) {
            Image(systemName: isDropTarget ? "arrow.down.circle.fill" : "arrow.down.doc")
                .font(.system(size: 28, weight: .light))
                .foregroundStyle(isDropTarget ? HColor.ink : HColor.faint)
            VStack(spacing: 4) {
                Text(isDropTarget ? "Release to plan the transfer" : "Drop music here")
                    .font(HFont.display(22))
                    .foregroundStyle(HColor.ink)
                Text("The app reads tags and sizes, then shows the plan. Nothing is copied yet.")
                    .font(HFont.body(11))
                    .foregroundStyle(HColor.muted)
            }
            HStack(spacing: 9) {
                Button("Choose music to transfer") {
                    model.prepareMusicTransfer()
                }
                .buttonStyle(PrimaryButtonStyle())
                Button("Add to library") {
                    model.chooseAudioFiles()
                }
                .buttonStyle(SecondaryButtonStyle())
            }
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 34)
        .background(isDropTarget ? HColor.paperDeep.opacity(0.7) : HColor.paperLight.opacity(0.45))
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .strokeBorder(
                    isDropTarget ? HColor.inkSoft : HColor.rule,
                    style: StrokeStyle(lineWidth: 1, dash: [5, 5])
                )
        )
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
        .onDrop(of: [UTType.fileURL.identifier], isTargeted: $isDropTarget) { providers in
            handleDrop(providers)
        }
    }

    private var emptyPlan: some View {
        HStack(alignment: .top, spacing: 14) {
            Image(systemName: "checklist")
                .font(.system(size: 15, weight: .light))
                .foregroundStyle(HColor.muted)
            VStack(alignment: .leading, spacing: 4) {
                Text("No transfer planned")
                    .font(HFont.body(11.5, weight: .semibold))
                    .foregroundStyle(HColor.ink)
                Text("A plan lists the source, destination, size and conflicts before the first byte is written. Podcasts and audiobooks use the same planner from their own screens.")
                    .font(HFont.body(10.5))
                    .foregroundStyle(HColor.muted)
                    .lineSpacing(2)
            }
            Spacer()
        }
        .padding(16)
        .background(HColor.paperDeep.opacity(0.35))
        .overlay(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
    }

    private func planCard(_ plan: TransferPlan) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(alignment: .firstTextBaseline) {
                Text("Transfer plan")
                    .font(HFont.display(22))
                    .foregroundStyle(HColor.ink)
                Spacer()
                Text("\(plan.items.count) files · \(plan.totalSizeText)")
                    .font(HFont.mono(10))
                    .foregroundStyle(HColor.muted)
            }
            .padding(.bottom, 9)
            HStack(spacing: 14) {
                Tag(text: plan.conflictPolicy.title, color: HColor.muted)
                if plan.conflictCount > 0 {
                    Tag(text: "\(plan.conflictCount) conflicts", color: HColor.rust)
                }
                Tag(text: model.settings.verifyTransfers ? "Verify on" : "Verify off", color: model.settings.verifyTransfers ? HColor.moss : HColor.ochre)
                let missing = model.missingMetadataCount(inPlan: plan)
                if missing > 0 {
                    Button {
                        model.section = .metadata
                    } label: {
                        Text("\(missing) files need tags")
                            .font(HFont.body(10, weight: .medium))
                            .foregroundStyle(HColor.ochre)
                    }
                    .buttonStyle(.plain)
                    .help("Open the metadata queue")
                }
                Spacer()
                Button("Clear") {
                    model.clearTransferPlan()
                }
                .buttonStyle(QuietButtonStyle())
                .disabled(model.isTransferring)
            }
            .padding(.bottom, 12)
            Hairline()
            ScrollView {
                LazyVStack(spacing: 0) {
                    ForEach(plan.items) { item in
                        TransferItemRow(item: item)
                        Hairline()
                    }
                }
            }
            .frame(maxHeight: 292)
            Hairline()
            HStack(spacing: 14) {
                Toggle("Verify checksums", isOn: $model.settings.verifyTransfers)
                    .toggleStyle(.checkbox)
                    .font(HFont.body(10.5))
                Toggle("Write MP3 tags", isOn: $model.settings.writeMP3TagsOnTransfer)
                    .toggleStyle(.checkbox)
                    .font(HFont.body(10.5))
                if plan.conflictCount > 0 {
                    Text("Replaced files are backed up first.")
                        .font(HFont.body(9.5))
                        .foregroundStyle(HColor.faint)
                }
                Spacer()
                if model.isTransferring {
                    VStack(alignment: .trailing, spacing: 5) {
                        Text(model.transferProgressText)
                            .font(HFont.mono(9.5))
                            .foregroundStyle(HColor.muted)
                            .lineLimit(1)
                        ProgressView()
                            .controlSize(.small)
                            .frame(width: 140)
                    }
                }
                Button {
                    Task { await model.executeTransfer() }
                } label: {
                    Label("Transfer \(plan.items.count) files", systemImage: "arrow.up.arrow.down")
                }
                .buttonStyle(PrimaryButtonStyle())
                .disabled(model.isTransferring)
            }
            .padding(.top, 13)
        }
        .padding(18)
        .background(HColor.paperLight)
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private func reportCard(_ report: TransferReport) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text(report.failed == 0 ? "Transfer complete" : "Transfer finished with errors")
                    .font(HFont.display(22))
                    .foregroundStyle(HColor.ink)
                Spacer()
                Image(systemName: report.failed == 0 ? "checkmark.seal" : "exclamationmark.triangle")
                    .font(.system(size: 17, weight: .light))
                    .foregroundStyle(report.failed == 0 ? HColor.moss : HColor.error)
            }
            HStack(spacing: 26) {
                ReportFact(value: "\(report.completed)", label: "Copied")
                ReportFact(value: "\(report.skipped)", label: "Skipped")
                ReportFact(value: "\(report.failed)", label: "Failed")
                ReportFact(value: Formatters.bytes(report.bytesTransferred), label: "Written")
                Spacer()
                if let backup = report.backupFolder {
                    Button("Show backup") {
                        NSWorkspace.shared.activateFileViewerSelecting([backup])
                    }
                    .buttonStyle(SecondaryButtonStyle())
                }
            }
            if !report.errors.isEmpty {
                Text(report.errors.prefix(5).joined(separator: "\n"))
                    .font(HFont.mono(9.5))
                    .foregroundStyle(HColor.error)
                    .textSelection(.enabled)
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

    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        let group = DispatchGroup()
        var urls: [URL] = []
        for provider in providers {
            group.enter()
            provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
                defer { group.leave() }
                var url: URL?
                if let data = item as? Data {
                    url = URL(dataRepresentation: data, relativeTo: nil)
                } else if let value = item as? URL {
                    url = value
                } else if let value = item as? String {
                    url = URL(string: value)
                }
                if let url {
                    urls.append(url)
                }
            }
        }
        group.notify(queue: .main) {
            guard !urls.isEmpty else { return }
            Task { await model.prepareMusicTransfer(urls: urls) }
        }
        return true
    }
}

private struct TransferItemRow: View {
    @EnvironmentObject private var model: AppModel
    let item: TransferItem

    private var track: Track? {
        model.snapshot.tracks.first(where: { $0.fileURL.standardizedFileURL == item.sourceURL.standardizedFileURL })
    }

    private var statusIcon: String {
        switch item.status {
        case .planned: return "circle"
        case .copying: return "arrow.down.circle"
        case .verifying: return "checkmark.shield"
        case .complete: return "checkmark.circle.fill"
        case .skipped: return "minus.circle"
        case .failed: return "xmark.circle.fill"
        }
    }

    private var statusColor: Color {
        switch item.status {
        case .complete: return HColor.moss
        case .failed: return HColor.error
        case .skipped: return HColor.faint
        case .verifying: return HColor.blue
        default: return HColor.muted
        }
    }

    var body: some View {
        HStack(alignment: .top, spacing: 11) {
            Image(systemName: statusIcon)
                .font(.system(size: 12, weight: .medium))
                .foregroundStyle(statusColor)
                .frame(width: 16)
                .padding(.top, 1)
            VStack(alignment: .leading, spacing: 4) {
                Text(item.sourceURL.lastPathComponent)
                    .font(HFont.body(11.5, weight: .medium))
                    .foregroundStyle(HColor.ink)
                    .lineLimit(1)
                Text(item.relativePath)
                    .font(HFont.mono(9))
                    .foregroundStyle(HColor.faint)
                    .lineLimit(1)
                    .truncationMode(.middle)
                if item.status == .copying || item.status == .verifying {
                    HProgressBar(value: item.progress, tint: HColor.inkSoft, height: 2)
                        .frame(maxWidth: 220)
                }
                if let error = item.error {
                    Text(error)
                        .font(HFont.body(9.5))
                        .foregroundStyle(HColor.error)
                        .lineLimit(2)
                }
            }
            Spacer()
            VStack(alignment: .trailing, spacing: 4) {
                Text(Formatters.bytes(item.byteCount))
                    .font(HFont.mono(9.5))
                    .foregroundStyle(HColor.muted)
                if item.conflict {
                    Tag(text: "Existing", color: HColor.rust)
                }
                if let track, !track.isMetadataComplete {
                    Tag(text: "\(track.missingFields.count) tags", color: HColor.ochre)
                }
                if item.status == .complete {
                    Text("verified")
                        .font(HFont.body(9))
                        .foregroundStyle(HColor.moss)
                }
            }
        }
        .padding(.vertical, 10)
    }
}

private struct ReportFact: View {
    let value: String
    let label: String

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(value)
                .font(HFont.display(19))
                .foregroundStyle(HColor.ink)
            Text(label)
                .font(HFont.body(9.5))
                .foregroundStyle(HColor.muted)
        }
    }
}
