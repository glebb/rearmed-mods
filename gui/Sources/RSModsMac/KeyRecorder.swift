import AppKit
import SwiftUI

// A key binding field: click it and press a key, as in the Windows GUI. Escape cancels, Delete clears.
struct KeyRecorder: View {
    @Binding var value: String
    var conflict = false

    @State private var recording = false
    @State private var monitor: Any?
    @State private var rejected = false

    var body: some View {
        Button(action: { recording ? stop() : start() }) {
            Text(label)
                .frame(minWidth: 110)
                .foregroundStyle(recording ? Color.accentColor : (conflict || !known ? Color.orange : Color.primary))
        }
        .help(help)
        .onDisappear(perform: stop)
    }

    private var known: Bool { KeyNames.isKnown(value) }

    private var label: String {
        if recording { return rejected ? "Key not supported" : "Press a key…" }
        if value.isEmpty { return "None" }
        return known ? KeyNames.display(value) : value
    }

    private var help: String {
        if recording { return "Press the key to bind. Escape cancels, Delete clears." }
        if !known { return "The mods don't know the key \"\(value)\"." }
        if conflict { return "This key is bound to more than one action." }
        return "Click, then press a key."
    }

    private func start() {
        recording = true
        rejected = false
        monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
            switch event.keyCode {
            case 0x35:  // Escape
                stop()
            case 0x33:  // Delete
                value = ""
                stop()
            default:
                if let name = KeyNames.iniValue(forKeyCode: event.keyCode) {
                    value = name
                    stop()
                } else {
                    rejected = true
                }
            }
            return nil
        }
    }

    private func stop() {
        recording = false
        if let monitor { NSEvent.removeMonitor(monitor) }
        monitor = nil
    }
}
