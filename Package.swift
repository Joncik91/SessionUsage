// swift-tools-version: 6.2
import CompilerPluginSupport
import Foundation
import PackageDescription

let sweetCookieKitPath = "../SweetCookieKit"
let packageEnvironment = ProcessInfo.processInfo.environment
let useLocalSweetCookieKit =
    (packageEnvironment["SESSIONUSAGE_USE_LOCAL_SWEETCOOKIEKIT"]
        ?? packageEnvironment["CODEXBAR_USE_LOCAL_SWEETCOOKIEKIT"]) == "1"
let sweetCookieKitDependency: Package.Dependency =
    useLocalSweetCookieKit && FileManager.default.fileExists(atPath: sweetCookieKitPath)
    ? .package(path: sweetCookieKitPath)
    : .package(url: "https://github.com/steipete/SweetCookieKit", from: "0.4.0")

var targets: [Target] = [
    .target(
        name: "CodexBarCore",
        dependencies: [
            "CodexBarMacroSupport",
            .product(name: "Logging", package: "swift-log"),
            .product(name: "SweetCookieKit", package: "SweetCookieKit"),
        ],
        swiftSettings: [
            .enableUpcomingFeature("StrictConcurrency"),
        ]),
    .macro(
        name: "CodexBarMacros",
        dependencies: [
            .product(name: "SwiftCompilerPlugin", package: "swift-syntax"),
            .product(name: "SwiftSyntaxBuilder", package: "swift-syntax"),
            .product(name: "SwiftSyntaxMacros", package: "swift-syntax"),
        ]),
    .target(
        name: "CodexBarMacroSupport",
        dependencies: [
            "CodexBarMacros",
        ]),
    .executableTarget(
        name: "SessionUsageCLI",
        dependencies: [
            "CodexBarCore",
            .product(name: "Commander", package: "Commander"),
        ],
        path: "Sources/SessionUsageCLI",
        swiftSettings: [
            .enableUpcomingFeature("StrictConcurrency"),
        ]),
    .testTarget(
        name: "SessionUsageLinuxTests",
        dependencies: ["CodexBarCore", "SessionUsageCLI"],
        path: "TestsLinux",
        swiftSettings: [
            .enableUpcomingFeature("StrictConcurrency"),
            .enableExperimentalFeature("SwiftTesting"),
        ]),
]

// The GTK tray target depends on system libraries (glib, gtk+-3.0,
// ayatana-appindicator, json-glib) that do not exist on macOS. Gate the
// Linux-only targets behind `#if os(Linux)` so `swift test` on macOS
// does not try to compile `Sources/SessionUsage/main.c`.
#if os(Linux)
targets.append(contentsOf: [
    // SwiftPM system-library targets are the documented way to wrap
    // system-installed C libraries for package consumers.
    // Source: https://docs.swift.org/package-manager/PackageDescription/Target/systemLibrary(name:path:pkgConfig:providers:swiftSettings:cSettings:cxxSettings:linkerSettings:)
    .systemLibrary(
        name: "CGtk3",
        pkgConfig: "gtk+-3.0",
        providers: [
            .apt(["libgtk-3-dev"]),
        ]),
    .systemLibrary(
        name: "CAyatanaAppIndicator3",
        pkgConfig: "ayatana-appindicator3-0.1",
        providers: [
            .apt(["libayatana-appindicator3-dev"]),
        ]),
    .systemLibrary(
        name: "CJSONGLib",
        pkgConfig: "json-glib-1.0",
        providers: [
            .apt(["libjson-glib-dev"]),
        ]),
    .executableTarget(
        name: "SessionUsage",
        dependencies: [
            "CGtk3",
            "CAyatanaAppIndicator3",
            "CJSONGLib",
        ],
        path: "Sources/SessionUsage"),
])
#endif

let package = Package(
    name: "SessionUsage",
    // macOS deployment target is driven by our dependencies: Commander 0.2.1
    // requires macOS 14, SweetCookieKit 0.4.0 requires macOS 13. Without this
    // declaration SwiftPM defaults to macOS 10.13 and `swift test` on macOS
    // fails at dependency resolution.
    platforms: [
        .macOS(.v14),
    ],
    dependencies: [
        .package(url: "https://github.com/steipete/Commander", from: "0.2.1"),
        .package(url: "https://github.com/apple/swift-log", from: "1.12.0"),
        .package(url: "https://github.com/apple/swift-syntax", from: "600.0.1"),
        sweetCookieKitDependency,
    ],
    targets: targets)
