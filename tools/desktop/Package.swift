// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "HelikonCompanion",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "HelikonCompanion", targets: ["HelikonCompanion"])
    ],
    targets: [
        .executableTarget(
            name: "HelikonCompanion",
            path: "Sources/HelikonCompanion",
            resources: [.process("Resources")],
            swiftSettings: [.swiftLanguageMode(.v5)]
        )
    ]
)
