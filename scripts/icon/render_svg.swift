// An SVG rendered to a PNG with the system's WebKit (no Chrome needed): swift render_svg.swift IN.svg OUT.png SIZE
import AppKit
import WebKit

let args = CommandLine.arguments
guard args.count == 4, let size = Double(args[3]) else { print("swift render_svg.swift IN.svg OUT.png SIZE"); exit(2) }
let svg = try String(contentsOfFile: args[1], encoding: .utf8)
let out = URL(fileURLWithPath: args[2])

final class Renderer: NSObject, WKNavigationDelegate {
    let web: WKWebView
    init(size: Double) {
        web = WKWebView(frame: NSRect(x: 0, y: 0, width: size, height: size))
        super.init()
        web.navigationDelegate = self
        web.setValue(false, forKey: "drawsBackground")
    }
    func webView(_ w: WKWebView, didFinish _: WKNavigation!) {
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
            let cfg = WKSnapshotConfiguration()
            cfg.rect = w.bounds
            cfg.snapshotWidth = NSNumber(value: Double(w.bounds.width))
            w.takeSnapshot(with: cfg) { image, error in
                guard let image, let tiff = image.tiffRepresentation, let rep = NSBitmapImageRep(data: tiff),
                      let png = rep.representation(using: .png, properties: [:]) else { print("snapshot failed: \(String(describing: error))"); exit(1) }
                try? png.write(to: out)
                exit(0)
            }
        }
    }
}

let app = NSApplication.shared
let r = Renderer(size: size)
r.web.loadHTMLString("<html><body style='margin:0;background:transparent'>\(svg)</body></html>", baseURL: nil)
app.run()
