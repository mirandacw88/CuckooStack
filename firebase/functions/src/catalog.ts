// The in-app products, from config/store/products.json (copied into src/ by `npm run build`).
import catalog from "./products.json";

export type ProductType = "consumable" | "non_consumable";
export interface StoreProduct { id: string; type: ProductType; coins: number; grants?: string[] }

const byId = new Map<string, StoreProduct>((catalog.products as StoreProduct[]).map((p) => [p.id, p]));

export function product(id: string): StoreProduct | undefined { return byId.get(id); }

// Which app this project serves: two Firebase projects, two app IDs (README: Environments).
export function appIdFor(projectId: string): string {
  return projectId === "cuckoostack-prod" ? "com.cuckoostack.aerospheregames" : "com.cuckoostack.aerospheregames.staging";
}
export function isProd(projectId: string): boolean { return projectId === "cuckoostack-prod"; }
