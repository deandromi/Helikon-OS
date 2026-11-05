import AppKit
import CompanionCore
import SwiftUI

struct RootView: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        HStack(spacing: 0) {
            SidebarView()
                .frame(width: 248)
            VerticalHairline()
            VStack(spacing: 0) {
                TopBarView()
                content
            }
            .background(HColor.paper)
        }
        .background(HColor.paper)
        .sheet(item: $model.metadataSearch) { context in
            MetadataResultsSheet(contextID: context.id)
                .environmentObject(model)
        }
        .alert(item: $model.alert) { alert in
            Alert(
                title: Text(alert.title),
                message: Text(alert.message),
                dismissButton: .default(Text("OK"))
            )
        }
        .overlay(alignment: .bottom) {
            if let status = model.statusMessage {
                Text(status)
                    .font(HFont.body(12, weight: .medium))
                    .foregroundStyle(HColor.paperLight)
                    .padding(.horizontal, 16)
                    .padding(.vertical, 9)
                    .background(HColor.ink)
                    .clipShape(Capsule())
                    .shadow(color: HColor.ink.opacity(0.14), radius: 12, y: 4)
                    .padding(.bottom, 22)
                    .transition(.move(edge: .bottom).combined(with: .opacity))
            }
        }
        .onChange(of: model.settings) { _, _ in
            model.saveSettings()
        }
        .animation(.easeInOut(duration: 0.18), value: model.statusMessage)
    }

    @ViewBuilder
    private var content: some View {
        switch model.section {
        case .overview: DashboardView()
        case .library: LibraryView()
        case .transfer: TransferView()
        case .metadata: MetadataView()
        case .podcasts: PodcastsView()
        case .audiobooks: AudiobooksView()
        case .scrobbler: ScrobblerView()
        case .install: InstallView()
        case .settings: SettingsView()
        }
    }
}

// MARK: - Top bar

struct TopBarView: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        HStack(spacing: 14) {
            EyebrowLabel(text: "\(model.section.index) / \(model.section.title)")
            Spacer()
            devicePill
            Button {
                Task { await model.refreshDevices() }
            } label: {
                Image(systemName: model.isRefreshingDevices ? "arrow.triangle.2.circlepath" : "arrow.clockwise")
                    .font(.system(size: 12, weight: .medium))
                    .foregroundStyle(HColor.muted)
                    .frame(width: 26, height: 26)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .help("Refresh mounted volumes")

            if [.overview, .library, .transfer, .podcasts, .audiobooks].contains(model.section) {
                Button {
                    model.chooseAudioFiles()
                } label: {
                    Label("Add music", systemImage: "plus")
                        .font(HFont.body(11.5, weight: .medium))
                        .foregroundStyle(HColor.paperLight)
                        .padding(.horizontal, 11)
                        .padding(.vertical, 6)
                        .background(HColor.ink)
                        .clipShape(RoundedRectangle(cornerRadius: 6, style: .continuous))
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, 28)
        .frame(height: 50)
        .background(HColor.paper.opacity(0.96))
        .overlay(alignment: .bottom) { Hairline() }
    }

    private var devicePill: some View {
        Button {
            if let device = model.selectedDevice {
                NSWorkspace.shared.activateFileViewerSelecting([device.mountURL])
            } else {
                Task { await model.refreshDevices() }
            }
        } label: {
            HStack(spacing: 7) {
                StatusDot(color: model.selectedDevice == nil ? HColor.faint : HColor.live, size: 5)
                Text(model.selectedDevice.map { "\($0.name) · \($0.kindLabel)" } ?? "No volume")
                    .font(HFont.body(11, weight: .medium))
                    .foregroundStyle(HColor.inkSoft)
                    .lineLimit(1)
                if let device = model.selectedDevice {
                    Text(Formatters.bytes(device.availableCapacity))
                        .font(HFont.mono(10))
                        .foregroundStyle(HColor.muted)
                }
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 6)
            .background(HColor.paperLight)
            .overlay(
                Capsule().stroke(HColor.rule, lineWidth: 1)
            )
            .clipShape(Capsule())
        }
        .buttonStyle(.plain)
    }
}

// MARK: - Sidebar

struct SidebarView: View {
    @EnvironmentObject private var model: AppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            brand
                .padding(.horizontal, 22)
                .padding(.top, 38)

            deviceCard
                .padding(.horizontal, 18)
                .padding(.top, 24)

            VStack(spacing: 2) {
                ForEach(CompanionSection.allCases.filter(\.primaryNavigation)) { section in
                    SidebarRow(section: section, selected: model.section == section) {
                        model.section = section
                    }
                }
            }
            .padding(.horizontal, 12)
            .padding(.top, 26)

            Spacer(minLength: 20)

            VStack(spacing: 2) {
                SidebarRow(section: .settings, selected: model.section == .settings) {
                    model.section = .settings
                }
            }
            .padding(.horizontal, 12)

            footer
                .padding(.horizontal, 22)
                .padding(.bottom, 20)
                .padding(.top, 16)
        }
        .background(HColor.paperDeep)
        .overlay(alignment: .trailing) { VerticalHairline() }
    }

    private var brand: some View {
        VStack(alignment: .leading, spacing: 9) {
            HStack(spacing: 10) {
                MountainMark()
                    .frame(width: 30, height: 19)
                Text("HELIKON")
                    .font(HFont.display(21, weight: .semibold))
                    .tracking(1.2)
                    .foregroundStyle(HColor.ink)
            }
            Text("COMPANION")
                .font(HFont.label(9.5, weight: .semibold))
                .tracking(3.0)
                .foregroundStyle(HColor.muted)
                .padding(.leading, 40)
        }
    }

    private var deviceCard: some View {
        VStack(alignment: .leading, spacing: 9) {
            HStack {
                EyebrowLabel(text: "Volume")
                Spacer()
                if model.demoMode {
                    Text("SAMPLE")
                        .font(HFont.label(8.5, weight: .bold))
                        .tracking(0.7)
                        .foregroundStyle(HColor.ochre)
                }
            }
            if let device = model.selectedDevice {
                HStack(spacing: 8) {
                    StatusDot(color: HColor.live, size: 6)
                    Text(device.name)
                        .font(HFont.body(12.5, weight: .medium))
                        .foregroundStyle(HColor.ink)
                        .lineLimit(1)
                }
                Text("\(device.kindLabel) · \(device.filesystem)")
                    .font(HFont.body(10.5))
                    .foregroundStyle(HColor.muted)
                HProgressBar(value: device.usedFraction, tint: HColor.inkSoft, height: 3)
                HStack {
                    Text(device.capacityText)
                        .font(HFont.mono(9.5))
                        .foregroundStyle(HColor.muted)
                    Spacer()
                    Button {
                        Task { await model.refreshDevices() }
                    } label: {
                        Image(systemName: "arrow.clockwise")
                            .font(.system(size: 9, weight: .medium))
                            .foregroundStyle(HColor.muted)
                    }
                    .buttonStyle(.plain)
                }
            } else {
                Text("No HFS+ volume")
                    .font(HFont.body(12.5, weight: .medium))
                    .foregroundStyle(HColor.ink)
                Text("Mount the iPod in Disk Mode, or use a test image.")
                    .font(HFont.body(10.5))
                    .foregroundStyle(HColor.muted)
                    .fixedSize(horizontal: false, vertical: true)
                Button {
                    Task { await model.refreshDevices() }
                } label: {
                    Text("Refresh volumes")
                        .font(HFont.body(10.5, weight: .medium))
                        .foregroundStyle(HColor.ink)
                }
                .buttonStyle(QuietButtonStyle())
            }
        }
        .padding(12)
        .background(HColor.paperLight.opacity(0.7))
        .overlay(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
    }

    private var footer: some View {
        VStack(alignment: .leading, spacing: 7) {
            Hairline()
                .padding(.bottom, 5)
            Text("PRE-RELEASE · \(AppModel.version)")
                .font(HFont.label(9, weight: .medium))
                .tracking(1)
                .foregroundStyle(HColor.muted)
            Text("The installer is not available in this build.")
                .font(HFont.body(9.5))
                .foregroundStyle(HColor.faint)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}

struct SidebarRow: View {
    let section: CompanionSection
    let selected: Bool
    let action: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: action) {
            HStack(spacing: 10) {
                Text(section.index)
                    .font(HFont.mono(9.5))
                    .foregroundStyle(selected ? HColor.paperLight.opacity(0.55) : HColor.faint)
                    .frame(width: 18, alignment: .leading)
                Image(systemName: section.icon)
                    .font(.system(size: 12, weight: .regular))
                    .frame(width: 16)
                Text(section.title)
                    .font(HFont.body(12.5, weight: selected ? .medium : .regular))
                Spacer(minLength: 0)
            }
            .foregroundStyle(selected ? HColor.paperLight : HColor.inkSoft)
            .padding(.horizontal, 10)
            .padding(.vertical, 8)
            .background(
                selected
                    ? HColor.ink
                    : (hovering ? HColor.paperLight.opacity(0.55) : Color.clear)
            )
            .clipShape(RoundedRectangle(cornerRadius: 6, style: .continuous))
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
    }
}

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
