// The core's C strings and JSON as Swift (Foundation only: the chunk store's tests build it on its own).
import Foundation

/// a C string the core handed over: as a Swift string, freed
func takeString(_ p: UnsafeMutablePointer<CChar>?) -> String? {
    guard let p else { return nil }
    defer { ed_free(p) }
    return String(cString: p)
}

func decodeJSON<T: Decodable>(_ type: T.Type, _ text: String?) -> T? {
    guard let text, let data = text.data(using: .utf8) else { return nil }
    do { return try JSONDecoder().decode(type, from: data) } catch {
        NSLog("[CuckooEditor] can't read %@: %@", String(describing: type), String(describing: error))
        return nil
    }
}
