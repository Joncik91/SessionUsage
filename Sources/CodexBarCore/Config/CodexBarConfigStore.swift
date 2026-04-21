import Foundation

public enum CodexBarConfigStoreError: LocalizedError {
    case invalidURL
    case decodeFailed(String)
    case encodeFailed(String)

    public var errorDescription: String? {
        switch self {
        case .invalidURL:
            "Invalid SessionUsage config path."
        case let .decodeFailed(details):
            "Failed to decode SessionUsage config: \(details)"
        case let .encodeFailed(details):
            "Failed to encode SessionUsage config: \(details)"
        }
    }
}

public struct CodexBarConfigStore: @unchecked Sendable {
    public let fileURL: URL
    private let fileManager: FileManager

    public init(fileURL: URL = Self.defaultURL(), fileManager: FileManager = .default) {
        self.fileURL = fileURL
        self.fileManager = fileManager
    }

    public func load() throws -> CodexBarConfig? {
        let resolvedURL = self.resolvedLoadURL()
        guard self.fileManager.fileExists(atPath: resolvedURL.path) else { return nil }
        let data = try Data(contentsOf: resolvedURL)
        let decoder = JSONDecoder()
        do {
            let decoded = try decoder.decode(CodexBarConfig.self, from: data)
            return decoded.normalized()
        } catch {
            throw CodexBarConfigStoreError.decodeFailed(error.localizedDescription)
        }
    }

    public func loadOrCreateDefault() throws -> CodexBarConfig {
        if let existing = try self.load() {
            if self.fileURL == Self.defaultURL(),
               !self.fileManager.fileExists(atPath: self.fileURL.path),
               let legacyURL = self.legacyDefaultURL,
               self.fileManager.fileExists(atPath: legacyURL.path)
            {
                try self.save(existing)
            }
            return existing
        }
        let config = CodexBarConfig.makeDefault()
        try self.save(config)
        return config
    }

    public func save(_ config: CodexBarConfig) throws {
        let normalized = config.normalized()
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let data: Data
        do {
            data = try encoder.encode(normalized)
        } catch {
            throw CodexBarConfigStoreError.encodeFailed(error.localizedDescription)
        }
        let directory = self.fileURL.deletingLastPathComponent()
        if !self.fileManager.fileExists(atPath: directory.path) {
            try self.fileManager.createDirectory(at: directory, withIntermediateDirectories: true)
        }
        try data.write(to: self.fileURL, options: [.atomic])
        try self.applySecurePermissionsIfNeeded()
    }

    public func deleteIfPresent() throws {
        guard self.fileManager.fileExists(atPath: self.fileURL.path) else { return }
        try self.fileManager.removeItem(at: self.fileURL)
    }

    public static func defaultURL(home: URL = FileManager.default.homeDirectoryForCurrentUser) -> URL {
        home
            .appendingPathComponent(".sessionusage", isDirectory: true)
            .appendingPathComponent("config.json")
    }

    public static func legacyDefaultURL(home: URL = FileManager.default.homeDirectoryForCurrentUser) -> URL {
        home
            .appendingPathComponent(".codexbar", isDirectory: true)
            .appendingPathComponent("config.json")
    }

    private var legacyDefaultURL: URL? {
        guard self.fileURL == Self.defaultURL() else { return nil }
        return Self.legacyDefaultURL()
    }

    private func resolvedLoadURL() -> URL {
        if self.fileManager.fileExists(atPath: self.fileURL.path) {
            return self.fileURL
        }
        if let legacyURL = self.legacyDefaultURL,
           self.fileManager.fileExists(atPath: legacyURL.path)
        {
            return legacyURL
        }
        return self.fileURL
    }

    private func applySecurePermissionsIfNeeded() throws {
        #if os(macOS) || os(Linux)
        try self.fileManager.setAttributes([
            .posixPermissions: NSNumber(value: Int16(0o600)),
        ], ofItemAtPath: self.fileURL.path)
        #endif
    }
}
