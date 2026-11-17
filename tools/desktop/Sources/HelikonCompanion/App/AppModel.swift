import Foundation
import SwiftUI

@MainActor
final class AppModel: ObservableObject {
    static let version = "0.1.0"

    @Published var isShowingSplash = true

    init() {
        if CommandLine.arguments.contains("--skip-splash") {
            isShowingSplash = false
        }
    }

    func dismissSplash() {
        isShowingSplash = false
    }

    func showSplash() {
        isShowingSplash = true
    }
}
