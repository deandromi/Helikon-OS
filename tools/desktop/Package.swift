// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "HelikonCompanion",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "HelikonCompanion", targets: ["HelikonCompanion"])
    ],
    targets: [
        .target(
            name: "CompanionCore",
            path: "Sources/CompanionCore",
            swiftSettings: [.swiftLanguageMode(.v5)],
            linkerSettings: [
                .linkedFramework("AVFoundation"),
                .linkedFramework("Security")
            ]
        ),
        .executableTarget(
            name: "HelikonCompanion",
            dependencies: ["CompanionCore"],
            path: "Sources/HelikonCompanion",
            resources: [.process("Resources")],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .testTarget(
            name: "CompanionCoreTests",
            dependencies: ["CompanionCore"],
            path: "Tests/CompanionCoreTests",
            swiftSettings: [.swiftLanguageMode(.v5)]
        )
    ]
)
