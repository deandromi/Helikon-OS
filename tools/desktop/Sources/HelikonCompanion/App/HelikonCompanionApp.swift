import SwiftUI

@main
struct HelikonCompanionApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var model = AppModel()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(model)
                .frame(minWidth: 1120, minHeight: 720)
                .preferredColorScheme(.light)
        }
        .windowStyle(.hiddenTitleBar)
        .commands {
            CommandGroup(replacing: .newItem) {}
            CommandMenu("Device") {
                Button("Refresh Volumes") {
                    Task { await model.refreshDevices() }
                }
                .keyboardShortcut("r", modifiers: [.command, .shift])
                Button("Back Up a Folder…") {
                    model.backupFolder()
                }
                .keyboardShortcut("b", modifiers: [.command, .shift])
            }
        }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApplication.shared.appearance = NSAppearance(named: .aqua)
        DispatchQueue.main.async {
            for window in NSApplication.shared.windows {
                window.isMovableByWindowBackground = true
                window.titlebarAppearsTransparent = true
                window.titleVisibility = .hidden
                window.backgroundColor = NSColor(calibratedRed: 0.95, green: 0.94, blue: 0.90, alpha: 1)
            }
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }
}
