// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "RSModsMac",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(name: "RSModsMac", path: "Sources/RSModsMac"),
        .testTarget(name: "RSModsMacTests", dependencies: ["RSModsMac"], path: "Tests/RSModsMacTests"),
    ]
)
