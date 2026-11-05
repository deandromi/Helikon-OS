import AppKit
import CompanionCore
import SwiftUI

struct InstallView: View {
    @EnvironmentObject private var model: AppModel

    private var readiness: InstallReadiness { model.installReadiness }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 24) {
                SectionTitle(
                    "Install",
                    kicker: "Release path",
                    subtitle: "The guide and preflight checks are here. The installer itself is deliberately absent until a signed firmware image and a recorded restore path exist."
                )
                banner
                HStack(alignment: .top, spacing: 20) {
                    preflight
                    rules
                        .frame(width: 318)
                }
                steps
                limitations
            }
            .padding(.horizontal, 32)
            .padding(.vertical, 30)
        }
        .background(HColor.paper)
    }

    private var banner: some View {
        HStack(alignment: .center, spacing: 22) {
            VStack(alignment: .leading, spacing: 10) {
                HStack(spacing: 8) {
                    Tag(text: "Not available", color: HColor.rust)
                    Tag(text: "No signed release", color: HColor.muted)
                }
                Text("The installer is not part of this build.")
                    .font(HFont.display(31))
                    .foregroundStyle(HColor.ink)
                Text("Helikon-OS is still pre-phase 0 for installation. This screen checks the conditions an installer will need, so the release flow is not invented later. It never formats, repartitions, restores or erases a volume.")
                    .font(HFont.body(12))
                    .foregroundStyle(HColor.muted)
                    .lineSpacing(3)
                    .frame(maxWidth: 660, alignment: .leading)
            }
            Spacer()
            VStack(alignment: .trailing, spacing: 8) {
                Image(systemName: "lock.shield")
                    .font(.system(size: 30, weight: .ultraLight))
                    .foregroundStyle(HColor.muted)
                Text("Signed image required")
                    .font(HFont.body(9.5))
                    .foregroundStyle(HColor.faint)
            }
        }
        .padding(24)
        .background(HColor.paperLight)
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
    }

    private var preflight: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("Preflight")
                    .font(HFont.display(21))
                    .foregroundStyle(HColor.ink)
                Spacer()
                Button {
                    Task { await model.refreshDevices() }
                } label: {
                    Text("Refresh")
                        .font(HFont.body(10.5, weight: .medium))
                        .foregroundStyle(HColor.muted)
                }
                .buttonStyle(.plain)
            }
            .padding(.bottom, 10)
            InstallCheckRow(
                title: "Volume mounted",
                detail: model.selectedDevice?.name ?? "No HFS+ volume found",
                state: model.selectedDevice == nil ? .blocked : .ready
            )
            InstallCheckRow(
                title: "HFS+ and writable",
                detail: model.selectedDevice.map { "\($0.filesystem) · \($0.statusLabel)" } ?? "Waiting for a volume",
                state: model.selectedDevice?.isHFS == true && model.selectedDevice?.isReadOnly == false ? .ready : .blocked
            )
            InstallCheckRow(
                title: "Verified backup",
                detail: model.backupVerified ? "A backup manifest was verified this session" : "No verified backup this session",
                state: model.backupVerified ? .ready : .blocked
            )
            InstallCheckRow(
                title: "Signed release",
                detail: "No production firmware or bootloader image exists",
                state: .blocked
            )
            HStack(spacing: 8) {
                Button("Back up a folder") { model.backupFolder() }
                    .buttonStyle(PrimaryButtonStyle())
                if model.selectedDevice != nil {
                    Button("Open volume") {
                        if let url = model.selectedDevice?.mountURL {
                            NSWorkspace.shared.activateFileViewerSelecting([url])
                        }
                    }
                    .buttonStyle(SecondaryButtonStyle())
                }
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

    private var rules: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Hard rules")
                .font(HFont.display(21))
                .foregroundStyle(HColor.ink)
            RuleRow("A verified backup comes first.")
            RuleRow("Bootloader changes are DFU-only.")
            RuleRow("Never install with --single.")
            RuleRow("No FAT, overlay or hidden fallback.")
            RuleRow("No format, erase or metadata repair.")
            RuleRow("A failed install must remain recoverable.")
            Text(readiness.summary)
                .font(HFont.body(10))
                .foregroundStyle(HColor.muted)
                .lineSpacing(2)
                .padding(.top, 4)
        }
        .padding(18)
        .background(HColor.paperDeep.opacity(0.4))
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private var steps: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("The release path")
                    .font(HFont.display(21))
                    .foregroundStyle(HColor.ink)
                Spacer()
                Text("Guide only")
                    .font(HFont.mono(9.5))
                    .foregroundStyle(HColor.muted)
            }
            .padding(.bottom, 10)
            ForEach(readiness.steps) { step in
                HStack(alignment: .top, spacing: 14) {
                    Text(String(format: "%02d", step.id))
                        .font(HFont.mono(9.5))
                        .foregroundStyle(HColor.faint)
                        .frame(width: 24, alignment: .leading)
                        .padding(.top, 2)
                    Circle()
                        .fill(step.state == .ready ? HColor.live : (step.state == .blocked ? HColor.ruleStrong : HColor.blue))
                        .frame(width: 6, height: 6)
                        .padding(.top, 5)
                    VStack(alignment: .leading, spacing: 3) {
                        HStack(spacing: 8) {
                            Text(step.title)
                                .font(HFont.body(12, weight: .medium))
                                .foregroundStyle(HColor.ink)
                            Tag(text: step.state.label, color: step.state == .ready ? HColor.moss : HColor.muted)
                        }
                        Text(step.detail)
                            .font(HFont.body(10.5))
                            .foregroundStyle(HColor.muted)
                            .lineSpacing(2)
                    }
                    Spacer()
                }
                .padding(.vertical, 11)
                Hairline()
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

    private var limitations: some View {
        VStack(alignment: .leading, spacing: 8) {
            EyebrowLabel(text: "What this build does not do")
            ForEach(readiness.limitations, id: \.self) { item in
                HStack(alignment: .top, spacing: 8) {
                    Text("—")
                        .font(HFont.body(10))
                        .foregroundStyle(HColor.faint)
                    Text(item)
                        .font(HFont.body(10.5))
                        .foregroundStyle(HColor.muted)
                }
            }
            Text("The installation guide will be completed with the first signed release, not before. Nothing on this screen writes to a device.")
                .font(HFont.body(9.5))
                .foregroundStyle(HColor.faint)
                .padding(.top, 3)
        }
        .padding(18)
        .background(HColor.paperDeep.opacity(0.35))
        .overlay(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
    }
}

private struct InstallCheckRow: View {
    let title: String
    let detail: String
    let state: InstallationStepState

    var body: some View {
        HStack(alignment: .top, spacing: 9) {
            Image(systemName: state == .ready ? "checkmark.circle.fill" : (state == .blocked ? "circle.dotted" : "info.circle"))
                .font(.system(size: 11))
                .foregroundStyle(state == .ready ? HColor.moss : HColor.faint)
                .padding(.top, 1)
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .font(HFont.body(11, weight: .medium))
                    .foregroundStyle(HColor.ink)
                Text(detail)
                    .font(HFont.body(9.5))
                    .foregroundStyle(HColor.muted)
            }
            Spacer()
        }
        .padding(.vertical, 7)
        .overlay(alignment: .bottom) { Hairline() }
    }
}

private struct RuleRow: View {
    let text: String

    init(_ text: String) { self.text = text }

    var body: some View {
        HStack(alignment: .top, spacing: 8) {
            Text("—")
                .font(HFont.body(10))
                .foregroundStyle(HColor.faint)
            Text(text)
                .font(HFont.body(10.5))
                .foregroundStyle(HColor.inkSoft)
        }
    }
}
