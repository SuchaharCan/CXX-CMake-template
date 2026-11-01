#!/usr/bin/env swift

import AppKit
import Foundation

let root = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
let output = root.appendingPathComponent("icons", isDirectory: true)
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)

let icons: [(name: String, symbol: String, color: NSColor)] = [
    ("project", "square.stack.3d.up.fill", .systemBlue),
    ("board", "rectangle.split.3x1.fill", .systemOrange),
    ("task", "checklist", .systemGreen),
    ("note", "note.text", .systemYellow),
    ("inbox", "tray.full.fill", .systemTeal),
]

for icon in icons {
    guard
        let symbol = NSImage(
            systemSymbolName: icon.symbol,
            accessibilityDescription: icon.name
        )
    else {
        fatalError("Missing SF Symbol: \(icon.symbol)")
    }

    let configuration = NSImage.SymbolConfiguration(pointSize: 84, weight: .semibold)
        .applying(NSImage.SymbolConfiguration(paletteColors: [icon.color]))
    guard let configured = symbol.withSymbolConfiguration(configuration) else {
        fatalError("Cannot configure SF Symbol: \(icon.symbol)")
    }

    let canvas = NSImage(size: NSSize(width: 128, height: 128))
    canvas.lockFocus()
    NSColor.clear.setFill()
    NSRect(x: 0, y: 0, width: 128, height: 128).fill()
    configured.draw(
        in: NSRect(x: 18, y: 18, width: 92, height: 92),
        from: .zero,
        operation: .sourceOver,
        fraction: 1
    )
    canvas.unlockFocus()

    guard
        let tiff = canvas.tiffRepresentation,
        let bitmap = NSBitmapImageRep(data: tiff),
        let png = bitmap.representation(using: .png, properties: [:])
    else {
        fatalError("Cannot render \(icon.name)")
    }
    try png.write(to: output.appendingPathComponent("\(icon.name).png"))
}
