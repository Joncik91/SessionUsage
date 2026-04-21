import CodexBarCore
import Foundation
import Testing

struct PathBuilderTests {
    @Test
    func `merges and deduplicates login shell path`() {
        let seeded = PathBuilder.effectivePATH(
            purposes: [.rpc],
            env: ["PATH": "/custom/bin:/usr/bin"],
            loginPATH: ["/login/bin", "/custom/bin"])

        #expect(seeded == "/login/bin:/custom/bin:/usr/bin")
    }

    @Test
    func `uses fallback when no path is available`() {
        let seeded = PathBuilder.effectivePATH(
            purposes: [.tty],
            env: [:],
            loginPATH: nil)

        #expect(seeded == "/usr/bin:/bin:/usr/sbin:/sbin")
    }

    @Test
    func `debug snapshot async matches sync`() async {
        let env = [
            "CODEX_CLI_PATH": "/usr/bin/true",
            "CLAUDE_CLI_PATH": "/usr/bin/true",
            "GEMINI_CLI_PATH": "/usr/bin/true",
            "PATH": "/usr/bin:/bin",
        ]

        let sync = PathBuilder.debugSnapshot(purposes: [.rpc], env: env, home: "/tmp")
        let async = await PathBuilder.debugSnapshotAsync(purposes: [.rpc], env: env, home: "/tmp")

        #expect(async == sync)
    }

    @Test
    func `resolves codex from explicit override`() throws {
        let sandbox = try TestSandbox()
        defer { sandbox.cleanup() }

        let overridePath = try sandbox.makeExecutable(relativePath: "bin/codex")
        let resolved = BinaryLocator.resolveCodexBinary(
            env: ["CODEX_CLI_PATH": overridePath],
            loginPATH: nil,
            fileManager: .default,
            home: sandbox.homePath)

        #expect(resolved == overridePath)
    }

    @Test
    func `prefers login path over environment path`() throws {
        let sandbox = try TestSandbox()
        defer { sandbox.cleanup() }

        let loginPath = try sandbox.makeExecutable(relativePath: "login/codex")
        _ = try sandbox.makeExecutable(relativePath: "env/codex")

        let resolved = BinaryLocator.resolveCodexBinary(
            env: ["PATH": "\(sandbox.rootPath)/env"],
            loginPATH: ["\(sandbox.rootPath)/login"],
            fileManager: .default,
            home: sandbox.homePath)

        #expect(resolved == loginPath)
    }

    @Test
    func `uses alias fallback when standard claude lookups fail`() throws {
        let sandbox = try TestSandbox()
        defer { sandbox.cleanup() }

        let aliasPath = try sandbox.makeHomeExecutable(relativePath: ".claude/local/claude")
        var aliasCalled = false
        let aliasResolver: (String, String?, TimeInterval, FileManager, String)
            -> String? = { tool, shell, timeout, _, home in
                aliasCalled = true
                #expect(tool == "claude")
                #expect(shell == "/bin/zsh")
                #expect(timeout == 2.0)
                #expect(home == sandbox.homePath)
                return aliasPath
            }

        let resolved = BinaryLocator.resolveClaudeBinary(
            env: ["SHELL": "/bin/zsh"],
            loginPATH: nil,
            commandV: { _, _, _, _ in nil },
            aliasResolver: aliasResolver,
            fileManager: .default,
            home: sandbox.homePath)

        #expect(aliasCalled)
        #expect(resolved == aliasPath)
    }

    @Test
    func `prefers shell discovery over well known claude paths`() throws {
        let sandbox = try TestSandbox()
        defer { sandbox.cleanup() }

        let shellPath = try sandbox.makeExecutable(relativePath: "shell/claude")
        _ = try sandbox.makeHomeExecutable(relativePath: ".claude/bin/claude")

        let resolved = BinaryLocator.resolveClaudeBinary(
            env: ["SHELL": "/bin/zsh"],
            loginPATH: nil,
            commandV: { _, _, _, _ in shellPath },
            aliasResolver: { _, _, _, _, _ in nil },
            fileManager: .default,
            home: sandbox.homePath)

        #expect(resolved == shellPath)
    }

    @Test
    func `falls back to downloaded claude binary when shims are missing`() throws {
        let sandbox = try TestSandbox()
        defer { sandbox.cleanup() }

        try sandbox.makeHomeDirectory(relativePath: ".local/bin/claude")
        let downloaded = try sandbox.makeHomeExecutable(relativePath: ".claude/downloads/claude-2.1.101-linux-x64")

        let resolved = BinaryLocator.resolveClaudeBinary(
            env: ["SHELL": "/bin/zsh"],
            loginPATH: nil,
            commandV: { _, _, _, _ in nil },
            aliasResolver: { _, _, _, _, _ in nil },
            fileManager: .default,
            home: sandbox.homePath)

        #expect(resolved == downloaded)
    }

    @Test
    func `ignores override when claude override points at a directory`() throws {
        let sandbox = try TestSandbox()
        defer { sandbox.cleanup() }

        let overrideDirectory = try sandbox.makeDirectory(relativePath: "overrides/claude")
        let downloaded = try sandbox.makeHomeExecutable(relativePath: ".claude/downloads/claude-2.1.101-linux-x64")

        let resolved = BinaryLocator.resolveClaudeBinary(
            env: [
                "CLAUDE_CLI_PATH": overrideDirectory,
                "SHELL": "/bin/zsh",
            ],
            loginPATH: nil,
            commandV: { _, _, _, _ in nil },
            aliasResolver: { _, _, _, _, _ in nil },
            fileManager: .default,
            home: sandbox.homePath)

        #expect(resolved == downloaded)
    }
}

private struct TestSandbox {
    let rootURL: URL
    let homeURL: URL

    var rootPath: String {
        self.rootURL.path
    }

    var homePath: String {
        self.homeURL.path
    }

    init() throws {
        self.rootURL = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        self.homeURL = self.rootURL.appendingPathComponent("home", isDirectory: true)

        try FileManager.default.createDirectory(at: self.homeURL, withIntermediateDirectories: true)
    }

    func cleanup() {
        try? FileManager.default.removeItem(at: self.rootURL)
    }

    @discardableResult
    func makeDirectory(relativePath: String) throws -> String {
        let url = self.rootURL.appendingPathComponent(relativePath, isDirectory: true)
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url.path
    }

    @discardableResult
    func makeExecutable(relativePath: String) throws -> String {
        let url = self.rootURL.appendingPathComponent(relativePath, isDirectory: false)
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try "#!/bin/sh\nexit 0\n".write(to: url, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: url.path)
        return url.path
    }

    @discardableResult
    func makeHomeDirectory(relativePath: String) throws -> String {
        let url = self.homeURL.appendingPathComponent(relativePath, isDirectory: true)
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url.path
    }

    @discardableResult
    func makeHomeExecutable(relativePath: String) throws -> String {
        let url = self.homeURL.appendingPathComponent(relativePath, isDirectory: false)
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try "#!/bin/sh\nexit 0\n".write(to: url, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: url.path)
        return url.path
    }
}
