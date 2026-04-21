import CodexBarCore
import Foundation
import Testing
@testable import CodexBarCLI

@Suite
struct ProviderPayloadContractTests {
    @Test
    func usagePayloadJSON_keepsTrayRelevantFieldsStable() throws {
        let snapshot = UsageSnapshot(
            primary: RateWindow(
                usedPercent: 18,
                windowMinutes: 300,
                resetsAt: nil,
                resetDescription: nil),
            secondary: nil,
            updatedAt: Date(timeIntervalSince1970: 1_700_000_000),
            identity: ProviderIdentitySnapshot(
                providerID: .claude,
                accountEmail: "test@example.com",
                accountOrganization: nil,
                loginMethod: "Max"))
        let payload = ProviderPayload(
            provider: .claude,
            account: nil,
            version: "1.2.3",
            source: "oauth",
            status: nil,
            usage: snapshot,
            credits: nil,
            antigravityPlanInfo: nil,
            openaiDashboard: nil,
            error: nil)

        let encoded = try #require(CodexBarCLI.encodeJSON([payload], pretty: false))
        let root = try #require(Self.decodeJSONArray(encoded).first)
        let usage = try #require(root["usage"] as? [String: Any])
        let primary = try #require(usage["primary"] as? [String: Any])
        let identity = try #require(usage["identity"] as? [String: Any])

        #expect(root["provider"] as? String == "claude")
        #expect(root["source"] as? String == "oauth")
        #expect(root["version"] as? String == "1.2.3")
        #expect(primary["usedPercent"] as? Double == 18)
        #expect(usage["secondary"] is NSNull)
        #expect(identity["accountEmail"] as? String == "test@example.com")
        #expect(identity["loginMethod"] as? String == "Max")
    }

    @Test
    func providerErrorPayloadJSON_includesStructuredErrorContext() throws {
        let payload = CodexBarCLI.makeProviderErrorPayload(
            provider: .copilot,
            account: nil,
            source: "api",
            status: nil,
            error: NSError(domain: "Tests", code: 7, userInfo: [NSLocalizedDescriptionKey: "Missing token"]),
            kind: .provider)

        let encoded = try #require(CodexBarCLI.encodeJSON([payload], pretty: false))
        let root = try #require(Self.decodeJSONArray(encoded).first)
        let error = try #require(root["error"] as? [String: Any])

        #expect(root["provider"] as? String == "copilot")
        #expect(root["source"] as? String == "api")
        #expect(error["message"] as? String == "Missing token")
        #expect(error["kind"] as? String == "provider")
    }

    @Test
    func copilotUsagePayloadJSON_preservesPremiumAndChatBuckets() throws {
        let snapshot = UsageSnapshot(
            primary: RateWindow(
                usedPercent: 25,
                windowMinutes: nil,
                resetsAt: nil,
                resetDescription: nil),
            secondary: RateWindow(
                usedPercent: 40,
                windowMinutes: nil,
                resetsAt: nil,
                resetDescription: nil),
            updatedAt: Date(timeIntervalSince1970: 1_700_000_000),
            identity: ProviderIdentitySnapshot(
                providerID: .copilot,
                accountEmail: nil,
                accountOrganization: nil,
                loginMethod: "Business"))
        let payload = ProviderPayload(
            provider: .copilot,
            account: nil,
            version: "1.2.3",
            source: "api",
            status: nil,
            usage: snapshot,
            credits: nil,
            antigravityPlanInfo: nil,
            openaiDashboard: nil,
            error: nil)

        let encoded = try #require(CodexBarCLI.encodeJSON([payload], pretty: false))
        let root = try #require(Self.decodeJSONArray(encoded).first)
        let usage = try #require(root["usage"] as? [String: Any])
        let primary = try #require(usage["primary"] as? [String: Any])
        let secondary = try #require(usage["secondary"] as? [String: Any])

        #expect(root["provider"] as? String == "copilot")
        #expect(root["source"] as? String == "api")
        #expect(primary["usedPercent"] as? Double == 25)
        #expect(secondary["usedPercent"] as? Double == 40)
    }

    private static func decodeJSONArray(_ text: String) throws -> [[String: Any]] {
        let data = try #require(text.data(using: .utf8))
        return try #require(JSONSerialization.jsonObject(with: data) as? [[String: Any]])
    }
}
