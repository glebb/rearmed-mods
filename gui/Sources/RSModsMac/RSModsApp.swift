import AppKit
import SwiftUI

@main
struct RSModsApp: App {
    @NSApplicationDelegateAdaptor private var delegate: AppDelegate
    @StateObject private var store = SettingsStore()
    @StateObject private var library = ModsLibrary()

    var body: some Scene {
        Window("Re-ARMed Mods", id: "main") {
            ContentView().environmentObject(store).environmentObject(library)
        }
        .windowResizability(.contentMinSize)
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        // Run from `swift run` (no app bundle) as a regular app with a Dock icon and a key window
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
