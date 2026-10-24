// A chunk open in the editor: the settings a chunk has beside its body (its name, description, ranges, weight, uses,
// Mirror), kept as the file's header. The body is the course (EditorModel, opened with `chunk:`): the same canvas, tools,
// checker, bot and history as a level's.
import SwiftUI

@MainActor
final class ChunkSession: ObservableObject {
    @Published var id: String
    @Published var header: ChunkHeader

    init(id: String, header: ChunkHeader) {
        self.id = id
        self.header = header
    }

    /// what the screens call it: its first description line, else its name
    var title: String { header.summary.isEmpty ? ChunkNames.shown(id).capitalized : header.summary }

    /// the chunk's file saved: its header and this body
    func save(body: String, to store: ChunkStore) { store.save(id, text: Chunks.text(header, body: body)) }
}

/// a band over the course (the canvas draws it): where a roll placed a chunk, or a chunk's pad
struct CanvasBand: Equatable {
    var x0: Double
    var x1: Double
    var label: String
    var hex: String
    var stripped: Bool      // a pad: the generator strips it when it places the chunk
}
