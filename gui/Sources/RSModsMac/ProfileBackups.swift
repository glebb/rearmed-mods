import Foundation

// RSMods' Backup Profile, the app's half: when the app opens, each save folder (Steam Cloud's
// <account>/221680/remote: the profiles and LocalProfiles.json) that changed since its last backup is copied to
// Profile_AutoBackups/<account>/Before GUI/<UTC time> in the game folder, and the newest NumberOfBackups are kept. The
// library backs up while the game runs, in the same folder (src/mod_profile_backups.c). RS Re-ARMed keeps an edition's
// save folders in its steam folder, next to its game folder.
enum ProfileBackups {
    static let rootName = "Profile_AutoBackups"
    static let tierName = "Before GUI"

    // The save folders under an edition's steam folder, by account
    static func saveFolders(steamFolder: URL) -> [(account: String, folder: URL)] {
        let fm = FileManager.default
        let accounts = (try? fm.contentsOfDirectory(atPath: steamFolder.path)) ?? []
        return accounts.sorted().compactMap { account in
            guard account.allSatisfy(\.isNumber) else { return nil }
            let folder = steamFolder.appendingPathComponent("\(account)/221680/remote", isDirectory: true)
            return fm.fileExists(atPath: folder.appendingPathComponent("LocalProfiles.json").path) ? (account, folder) : nil
        }
    }

    // A folder's files: name, size and modified time (to the millisecond)
    static func state(of folder: URL) -> [String: String] {
        let keys: [URLResourceKey] = [.isRegularFileKey, .fileSizeKey, .contentModificationDateKey]
        let files = (try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: keys)) ?? []
        var out: [String: String] = [:]
        for f in files {
            guard let v = try? f.resourceValues(forKeys: Set(keys)), v.isRegularFile == true else { continue }
            let ms = Int(((v.contentModificationDate?.timeIntervalSince1970) ?? 0) * 1000)
            out[f.lastPathComponent] = "\(v.fileSize ?? -1) \(ms)"
        }
        return out
    }

    static let nameFormat: DateFormatter = {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.timeZone = TimeZone(identifier: "UTC")
        f.dateFormat = "yyyy-MM-dd_HH-mm-ss"
        return f
    }()

    // A tier's backups, oldest first (folders named by their UTC time)
    static func backups(in tier: URL) -> [URL] {
        let names = (try? FileManager.default.contentsOfDirectory(atPath: tier.path)) ?? []
        return names.filter { nameFormat.date(from: $0).map { nameFormat.string(from: $0) } == $0 }.sorted()
            .map { tier.appendingPathComponent($0, isDirectory: true) }
    }

    // Backs up every save folder of the edition whose game folder this is; the backups made
    @discardableResult
    static func backUp(gameFolder: URL, keep: Int, now: Date = Date()) -> [URL] {
        let fm = FileManager.default
        let steam = gameFolder.deletingLastPathComponent().appendingPathComponent("steam", isDirectory: true)
        var made: [URL] = []
        for (account, folder) in saveFolders(steamFolder: steam) {
            let tier = gameFolder.appendingPathComponent("\(rootName)/\(account)/\(tierName)", isDirectory: true)
            let current = state(of: folder)
            if current.isEmpty { continue }
            if let newest = backups(in: tier).last, state(of: newest) == current { continue }
            let target = tier.appendingPathComponent(nameFormat.string(from: now), isDirectory: true)
            let partial = tier.appendingPathComponent(".partial_" + target.lastPathComponent, isDirectory: true)
            do {
                try? fm.removeItem(at: partial)
                try fm.createDirectory(at: tier, withIntermediateDirectories: true)
                try fm.copyItem(at: folder, to: partial)  // keeps the files' times
                guard state(of: folder) == current, state(of: partial) == current, !fm.fileExists(atPath: target.path) else {
                    try? fm.removeItem(at: partial)
                    continue
                }
                try fm.moveItem(at: partial, to: target)
                made.append(target)
            } catch {
                try? fm.removeItem(at: partial)
                continue
            }
            let all = backups(in: tier)
            for old in all.dropLast(max(keep, 1)) { try? fm.removeItem(at: old) }
        }
        return made
    }
}
