package com.cuckoostack.aerospheregames;

import android.app.Activity;
import android.util.Log;

import androidx.annotation.NonNull;

import com.android.billingclient.api.AcknowledgePurchaseParams;
import com.android.billingclient.api.BillingClient;
import com.android.billingclient.api.BillingClientStateListener;
import com.android.billingclient.api.BillingFlowParams;
import com.android.billingclient.api.BillingResult;
import com.android.billingclient.api.ConsumeParams;
import com.android.billingclient.api.PendingPurchasesParams;
import com.android.billingclient.api.ProductDetails;
import com.android.billingclient.api.Purchase;
import com.android.billingclient.api.QueryProductDetailsParams;
import com.android.billingclient.api.QueryPurchasesParams;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentLinkedQueue;

/**
 * Google Play Billing 8 for the in-app products in src/core/Economy.h (kProducts).
 * Consumables (coin packs) are verified by the verifyPurchase Cloud Function, then consumed, then reported to the
 * game; non-consumables (remove_ads, starter_pack) are acknowledged and re-reported on every launch, so owning them
 * survives reinstalls. The game polls {@link #pollEvent()} every frame.
 */
final class StoreManager {
    private static final String TAG = "CuckooStack";
    private static final List<String> CONSUMABLES = List.of("coins_s", "coins_m", "coins_l", "coins_xl", "coins_xxl");
    private static final List<String> NON_CONSUMABLES = List.of("remove_ads", "starter_pack");
    static final int SUCCESS = 0, CANCELLED = 1, FAILED = 2, PENDING = 3; // mirrors cs::PurchaseResult

    private final Activity activity;
    private final FirebaseBridge firebase;
    private final BillingClient billing;
    private final Map<String, ProductDetails> details = new ConcurrentHashMap<>();
    private final ConcurrentLinkedQueue<String> events = new ConcurrentLinkedQueue<>();
    private final Set<String> handled = Collections.newSetFromMap(new ConcurrentHashMap<>()); // purchase tokens in flight / done
    private volatile String launched = ""; // product of the purchase sheet on screen (cancel / failure reports name it)

    StoreManager(Activity activity, FirebaseBridge firebase) {
        this.activity = activity;
        this.firebase = firebase;
        billing = BillingClient.newBuilder(activity)
                .setListener(this::onPurchasesUpdated)
                .enablePendingPurchases(PendingPurchasesParams.newBuilder().enableOneTimeProducts().build())
                .enableAutoServiceReconnection()
                .build();
        billing.startConnection(new BillingClientStateListener() {
            @Override
            public void onBillingSetupFinished(@NonNull BillingResult r) {
                if (r.getResponseCode() != BillingClient.BillingResponseCode.OK) { Log.w(TAG, "Purchases: billing setup " + r.getDebugMessage()); return; }
                Log.i(TAG, "Purchases: billing connected");
                queryProducts();
                queryOwned(false);
            }

            @Override
            public void onBillingServiceDisconnected() { Log.w(TAG, "Purchases: billing disconnected"); }
        });
    }

    private void queryProducts() {
        List<QueryProductDetailsParams.Product> list = new ArrayList<>();
        for (String id : CONSUMABLES) list.add(QueryProductDetailsParams.Product.newBuilder().setProductId(id).setProductType(BillingClient.ProductType.INAPP).build());
        for (String id : NON_CONSUMABLES) list.add(QueryProductDetailsParams.Product.newBuilder().setProductId(id).setProductType(BillingClient.ProductType.INAPP).build());
        billing.queryProductDetailsAsync(QueryProductDetailsParams.newBuilder().setProductList(list).build(), (r, result) -> {
            if (r.getResponseCode() != BillingClient.BillingResponseCode.OK) { Log.w(TAG, "Purchases: product query " + r.getDebugMessage()); return; }
            for (ProductDetails d : result.getProductDetailsList()) details.put(d.getProductId(), d);
            Log.i(TAG, "Purchases: " + details.size() + " products");
        });
    }

    /** Owned purchases: finish any left unfinished (crash mid-purchase) and re-report non-consumables. */
    private void queryOwned(boolean restoring) {
        billing.queryPurchasesAsync(QueryPurchasesParams.newBuilder().setProductType(BillingClient.ProductType.INAPP).build(), (r, purchases) -> {
            if (r.getResponseCode() != BillingClient.BillingResponseCode.OK) return;
            for (Purchase p : purchases) handle(p, true);
        });
    }

    // ---- called from native code (CuckooActivity)
    /** "id|price;id|price" for every loaded product (localised prices). */
    String products() {
        StringBuilder sb = new StringBuilder();
        for (ProductDetails d : details.values()) {
            ProductDetails.OneTimePurchaseOfferDetails o = d.getOneTimePurchaseOfferDetails();
            if (o == null) continue;
            if (sb.length() > 0) sb.append(';');
            sb.append(d.getProductId()).append('|').append(o.getFormattedPrice().replace(";", "").replace("|", ""));
        }
        return sb.toString();
    }

    boolean purchase(String productId) {
        final ProductDetails d = details.get(productId);
        if (d == null || !billing.isReady()) return false;
        launched = productId;
        activity.runOnUiThread(() -> {
            BillingFlowParams params = BillingFlowParams.newBuilder()
                    .setProductDetailsParamsList(List.of(BillingFlowParams.ProductDetailsParams.newBuilder().setProductDetails(d).build()))
                    .build();
            BillingResult r = billing.launchBillingFlow(activity, params);
            if (r.getResponseCode() != BillingClient.BillingResponseCode.OK) push(productId, FAILED, false);
        });
        return true;
    }

    void restore() { queryOwned(true); }

    /** "id|result|restored" or null. */
    String pollEvent() { return events.poll(); }

    // ---- purchase handling
    private void push(String id, int result, boolean restored) { events.add(id + "|" + result + "|" + (restored ? 1 : 0)); }

    private void onPurchasesUpdated(@NonNull BillingResult r, List<Purchase> purchases) {
        final int code = r.getResponseCode();
        if (code == BillingClient.BillingResponseCode.OK && purchases != null) { for (Purchase p : purchases) handle(p, false); return; }
        if (code == BillingClient.BillingResponseCode.USER_CANCELED) { push(launched, CANCELLED, false); return; }
        if (code == BillingClient.BillingResponseCode.ITEM_ALREADY_OWNED) { queryOwned(true); return; }
        Log.w(TAG, "Purchases: purchase failed " + code + " " + r.getDebugMessage());
        push(launched, FAILED, false);
    }

    private void handle(Purchase p, boolean fromQuery) {
        if (p.getProducts().isEmpty()) return;
        final String id = p.getProducts().get(0);
        if (p.getPurchaseState() == Purchase.PurchaseState.PENDING) { if (!fromQuery) push(id, PENDING, false); return; }
        if (p.getPurchaseState() != Purchase.PurchaseState.PURCHASED) return;
        final String token = p.getPurchaseToken();
        if (CONSUMABLES.contains(id)) {
            if (!handled.add(token)) return;
            firebase.verifyPurchase(id, token, p.getOrderId(), valid -> {
                if (!valid) { Log.w(TAG, "Purchases: " + id + " rejected by the server"); push(id, FAILED, false); return; }
                billing.consumeAsync(ConsumeParams.newBuilder().setPurchaseToken(token).build(), (cr, t) -> {
                    if (cr.getResponseCode() == BillingClient.BillingResponseCode.OK) push(id, SUCCESS, false);
                    else { handled.remove(token); Log.w(TAG, "Purchases: consume failed " + cr.getDebugMessage()); }
                });
            });
        } else if (NON_CONSUMABLES.contains(id)) {
            if (p.isAcknowledged()) { push(id, SUCCESS, true); return; } // owned already: restore
            if (!handled.add(token)) return;
            firebase.verifyPurchase(id, token, p.getOrderId(), valid -> {
                if (!valid) { push(id, FAILED, false); return; }
                billing.acknowledgePurchase(AcknowledgePurchaseParams.newBuilder().setPurchaseToken(token).build(), ar -> {
                    if (ar.getResponseCode() == BillingClient.BillingResponseCode.OK) push(id, SUCCESS, fromQuery);
                    else handled.remove(token);
                });
            });
        }
    }
}
