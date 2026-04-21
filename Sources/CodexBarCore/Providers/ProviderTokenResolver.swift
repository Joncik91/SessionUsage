import Foundation

public enum ProviderTokenSource: String, Sendable {
    case environment
    case authFile
    case ghCLI
}

public struct ProviderTokenResolution: Sendable {
    public let token: String
    public let source: ProviderTokenSource

    public init(token: String, source: ProviderTokenSource) {
        self.token = token
        self.source = source
    }
}

public enum ProviderTokenResolver {
    public static func zaiToken(environment: [String: String] = ProcessInfo.processInfo.environment) -> String? {
        self.zaiResolution(environment: environment)?.token
    }

    public static func syntheticToken(
        environment: [String: String] = ProcessInfo.processInfo.environment) -> String?
    {
        self.syntheticResolution(environment: environment)?.token
    }

    public static func copilotToken(environment: [String: String] = ProcessInfo.processInfo.environment) -> String? {
        self.copilotResolution(environment: environment)?.token
    }

    public static func minimaxToken(environment: [String: String] = ProcessInfo.processInfo.environment) -> String? {
        self.minimaxTokenResolution(environment: environment)?.token
    }

    public static func alibabaToken(environment: [String: String] = ProcessInfo.processInfo.environment) -> String? {
        self.alibabaTokenResolution(environment: environment)?.token
    }

    public static func minimaxCookie(environment: [String: String] = ProcessInfo.processInfo.environment) -> String? {
        self.minimaxCookieResolution(environment: environment)?.token
    }

    public static func kimiAuthToken(environment: [String: String] = ProcessInfo.processInfo.environment) -> String? {
        self.kimiAuthResolution(environment: environment)?.token
    }

    public static func kimiK2Token(environment: [String: String] = ProcessInfo.processInfo.environment) -> String? {
        self.kimiK2Resolution(environment: environment)?.token
    }

    public static func kiloToken(
        environment: [String: String] = ProcessInfo.processInfo.environment,
        authFileURL: URL? = nil) -> String?
    {
        self.kiloResolution(environment: environment, authFileURL: authFileURL)?.token
    }

    public static func warpToken(environment: [String: String] = ProcessInfo.processInfo.environment) -> String? {
        self.warpResolution(environment: environment)?.token
    }

    public static func openRouterToken(environment: [String: String] = ProcessInfo.processInfo.environment) -> String? {
        self.openRouterResolution(environment: environment)?.token
    }

    public static func perplexitySessionToken(
        environment: [String: String] = ProcessInfo.processInfo.environment) -> String?
    {
        self.perplexityResolution(environment: environment)?.token
    }

    public static func zaiResolution(
        environment: [String: String] = ProcessInfo.processInfo.environment) -> ProviderTokenResolution?
    {
        self.resolveEnv(ZaiSettingsReader.apiToken(environment: environment))
    }

    public static func syntheticResolution(
        environment: [String: String] = ProcessInfo.processInfo.environment) -> ProviderTokenResolution?
    {
        self.resolveEnv(SyntheticSettingsReader.apiKey(environment: environment))
    }

    public static func copilotResolution(
        environment: [String: String] = ProcessInfo.processInfo.environment) -> ProviderTokenResolution?
    {
        self.copilotResolution(environment: environment, ghTokenProvider: self.copilotGitHubCLIToken)
    }

    static func copilotResolution(
        environment: [String: String],
        ghTokenProvider: ([String: String]) -> String?) -> ProviderTokenResolution?
    {
        if let resolution = self.resolveEnv(self.cleaned(environment["COPILOT_API_TOKEN"])) {
            return resolution
        }
        if let token = self.cleaned(ghTokenProvider(environment)) {
            return ProviderTokenResolution(token: token, source: .ghCLI)
        }
        return nil
    }

    public static func minimaxTokenResolution(
        environment: [String: String] = ProcessInfo.processInfo.environment) -> ProviderTokenResolution?
    {
        self.resolveEnv(MiniMaxAPISettingsReader.apiToken(environment: environment))
    }

    public static func alibabaTokenResolution(
        environment: [String: String] = ProcessInfo.processInfo.environment) -> ProviderTokenResolution?
    {
        self.resolveEnv(AlibabaCodingPlanSettingsReader.apiToken(environment: environment))
    }

    public static func minimaxCookieResolution(
        environment: [String: String] = ProcessInfo.processInfo.environment) -> ProviderTokenResolution?
    {
        self.resolveEnv(MiniMaxSettingsReader.cookieHeader(environment: environment))
    }

    public static func kimiAuthResolution(
        environment: [String: String] = ProcessInfo.processInfo.environment) -> ProviderTokenResolution?
    {
        if let resolution = self.resolveEnv(KimiSettingsReader.authToken(environment: environment)) {
            return resolution
        }
        #if os(macOS)
        do {
            let session = try KimiCookieImporter.importSession()
            if let token = session.authToken {
                return ProviderTokenResolution(token: token, source: .environment)
            }
        } catch {
            // No browser cookies found, continue to fallback
        }
        #endif
        return nil
    }

    public static func kimiK2Resolution(
        environment: [String: String] = ProcessInfo.processInfo.environment) -> ProviderTokenResolution?
    {
        self.resolveEnv(KimiK2SettingsReader.apiKey(environment: environment))
    }

    public static func kiloResolution(
        environment: [String: String] = ProcessInfo.processInfo.environment,
        authFileURL: URL? = nil) -> ProviderTokenResolution?
    {
        if let resolution = self.resolveEnv(KiloSettingsReader.apiKey(environment: environment)) {
            return resolution
        }
        if let token = KiloSettingsReader.authToken(authFileURL: authFileURL) {
            return ProviderTokenResolution(token: token, source: .authFile)
        }
        return nil
    }

    public static func warpResolution(
        environment: [String: String] = ProcessInfo.processInfo.environment) -> ProviderTokenResolution?
    {
        self.resolveEnv(WarpSettingsReader.apiKey(environment: environment))
    }

    public static func openRouterResolution(
        environment: [String: String] = ProcessInfo.processInfo.environment) -> ProviderTokenResolution?
    {
        self.resolveEnv(OpenRouterSettingsReader.apiToken(environment: environment))
    }

    public static func perplexityResolution(
        environment: [String: String] = ProcessInfo.processInfo.environment) -> ProviderTokenResolution?
    {
        if let resolution = self.resolveEnv(PerplexitySettingsReader.sessionToken(environment: environment)) {
            return resolution
        }
        #if os(macOS)
        do {
            let session = try PerplexityCookieImporter.importSession()
            if let token = session.sessionToken {
                return ProviderTokenResolution(token: token, source: .environment)
            }
        } catch {
            // No browser cookies found, continue to fallback
        }
        #endif
        return nil
    }

    private static func cleaned(_ raw: String?) -> String? {
        guard var value = raw?.trimmingCharacters(in: .whitespacesAndNewlines), !value.isEmpty else {
            return nil
        }

        if (value.hasPrefix("\"") && value.hasSuffix("\"")) ||
            (value.hasPrefix("'") && value.hasSuffix("'"))
        {
            value.removeFirst()
            value.removeLast()
        }

        value = value.trimmingCharacters(in: .whitespacesAndNewlines)
        return value.isEmpty ? nil : value
    }

    private static func resolveEnv(_ token: String?) -> ProviderTokenResolution? {
        guard let token else { return nil }
        return ProviderTokenResolution(token: token, source: .environment)
    }

    private static func copilotGitHubCLIToken(environment: [String: String]) -> String? {
        guard let ghBinary = self.resolveGitHubCLIBinary(environment: environment) else {
            return nil
        }

        let process = Process()
        process.executableURL = URL(fileURLWithPath: ghBinary)
        process.arguments = ["auth", "token"]

        var processEnvironment = ProcessInfo.processInfo.environment
        processEnvironment.merge(environment) { _, new in new }
        process.environment = processEnvironment

        let stdout = Pipe()
        process.standardOutput = stdout
        process.standardError = Pipe()

        do {
            try process.run()
        } catch {
            return nil
        }

        process.waitUntilExit()
        guard process.terminationStatus == 0 else { return nil }

        let data = stdout.fileHandleForReading.readDataToEndOfFile()
        return String(data: data, encoding: .utf8)
    }

    private static func resolveGitHubCLIBinary(
        environment: [String: String],
        fileManager: FileManager = .default) -> String?
    {
        if let existingPATH = environment["PATH"],
           let pathHit = self.findExecutable(
               "gh",
               in: existingPATH.split(separator: ":").map(String.init),
               fileManager: fileManager)
        {
            return pathHit
        }

        let fallback = [
            "/usr/local/bin",
            "/usr/bin",
            "/bin",
            "/snap/bin",
        ]
        return self.findExecutable("gh", in: fallback, fileManager: fileManager)
    }

    private static func findExecutable(_ binary: String, in paths: [String], fileManager: FileManager) -> String? {
        for path in paths where !path.isEmpty {
            let candidate = "\(path.hasSuffix("/") ? String(path.dropLast()) : path)/\(binary)"
            if fileManager.isExecutableFile(atPath: candidate) {
                return candidate
            }
        }
        return nil
    }
}
