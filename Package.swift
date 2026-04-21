// swift-tools-version: 6.2
import CompilerPluginSupport
import Foundation
import PackageDescription

let sweetCookieKitPath = "../SweetCookieKit"
let useLocalSweetCookieKit =
    ProcessInfo.processInfo.environment["CODEXBAR_USE_LOCAL_SWEETCOOKIEKIT"] == "1"
let sweetCookieKitDependency: Package.Dependency =
    useLocalSweetCookieKit && FileManager.default.fileExists(atPath: sweetCookieKitPath)
    ? .package(path: sweetCookieKitPath)
    : .package(url: "https://github.com/steipete/SweetCookieKit", from: "0.4.0")

let package = Package(
    name: "CodexBar",
    dependencies: [
        .package(url: "https://github.com/steipete/Commander", from: "0.2.1"),
        .package(url: "https://github.com/apple/swift-log", from: "1.12.0"),
        .package(url: "https://github.com/apple/swift-syntax", from: "600.0.1"),
        sweetCookieKitDependency,
    ],
    targets: [
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
            name: "CodexBarCLI",
            dependencies: [
                "CodexBarCore",
                .product(name: "Commander", package: "Commander"),
            ],
            path: "Sources/CodexBarCLI",
            swiftSettings: [
                .enableUpcomingFeature("StrictConcurrency"),
            ]),
        .testTarget(
            name: "CodexBarLinuxTests",
            dependencies: ["CodexBarCore", "CodexBarCLI"],
            path: "TestsLinux",
            swiftSettings: [
                .enableUpcomingFeature("StrictConcurrency"),
                .enableExperimentalFeature("SwiftTesting"),
            ]),
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
            name: "CodexBarLinux",
            dependencies: [
                "CGtk3",
                "CAyatanaAppIndicator3",
                "CJSONGLib",
            ],
            path: "Sources/CodexBarLinux"),
    ])
