import Foundation
import SwiftUI

@MainActor
final class AppModel: ObservableObject {
    static let version = "0.1.0"

    /// The splash is a first-run card, not an interstitial on every launch.
    /// The welcome item in the app menu reopens it on demand.
    static let hasSeenSplashKey = "com.helikon.companion.hasSeenSplash"

    @Published var isShowingSplash: Bool

    private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        let skip = CommandLine.arguments.contains("--skip-splash")
        let seen = defaults.bool(forKey: Self.hasSeenSplashKey)
        isShowingSplash = !skip && !seen
    }

    func dismissSplash() {
        isShowingSplash = false
        defaults.set(true, forKey: Self.hasSeenSplashKey)
    }

    func showSplash() {
        isShowingSplash = true
    }
}
