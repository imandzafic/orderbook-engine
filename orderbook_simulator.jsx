import { useState, useCallback, useRef, useEffect } from "react";

// ─── Order Book Engine (JS mirror of the C++ engine) ─────────────────
let nextId = 1;

function createEngine() {
  const bids = []; // sorted descending by price
  const asks = []; // sorted ascending by price
  const executions = [];
  const stats = { adds: 0, cancels: 0, matches: 0, totalMatchNs: 0 };

  function addOrder(side, price, qty) {
    const id = nextId++;
    const order = { id, side, price, qty, origQty: qty, ts: Date.now(), status: "active" };
    stats.adds++;

    // Match
    const t0 = performance.now();
    if (side === "buy") {
      matchAgainstAsks(order);
    } else {
      matchAgainstBids(order);
    }
    const t1 = performance.now();
    stats.totalMatchNs += (t1 - t0) * 1e6;
    stats.matches++;

    // Insert remainder
    if (order.qty > 0 && order.status === "active") {
      if (side === "buy") {
        bids.push(order);
        bids.sort((a, b) => b.price - a.price || a.ts - b.ts);
      } else {
        asks.push(order);
        asks.sort((a, b) => a.price - b.price || a.ts - b.ts);
      }
    }
    return { id, order };
  }

  function matchAgainstAsks(buyer) {
    while (buyer.qty > 0 && asks.length > 0) {
      const seller = asks[0];
      if (buyer.price < seller.price) break;
      const fillQty = Math.min(buyer.qty, seller.qty);
      executions.unshift({
        id: executions.length + 1,
        aggressor: buyer.id,
        passive: seller.id,
        price: seller.price,
        qty: fillQty,
        side: "buy",
        ts: Date.now(),
      });
      buyer.qty -= fillQty;
      seller.qty -= fillQty;
      if (seller.qty === 0) {
        seller.status = "filled";
        asks.shift();
      }
      if (buyer.qty === 0) buyer.status = "filled";
    }
  }

  function matchAgainstBids(seller) {
    while (seller.qty > 0 && bids.length > 0) {
      const buyer = bids[0];
      if (seller.price > buyer.price) break;
      const fillQty = Math.min(seller.qty, buyer.qty);
      executions.unshift({
        id: executions.length + 1,
        aggressor: seller.id,
        passive: buyer.id,
        price: buyer.price,
        qty: fillQty,
        side: "sell",
        ts: Date.now(),
      });
      seller.qty -= fillQty;
      buyer.qty -= fillQty;
      if (buyer.qty === 0) {
        buyer.status = "filled";
        bids.shift();
      }
      if (seller.qty === 0) seller.status = "filled";
    }
  }

  function cancelOrder(id) {
    let idx = bids.findIndex((o) => o.id === id);
    if (idx >= 0) { bids.splice(idx, 1); stats.cancels++; return true; }
    idx = asks.findIndex((o) => o.id === id);
    if (idx >= 0) { asks.splice(idx, 1); stats.cancels++; return true; }
    return false;
  }

  function getAggregatedBids(maxLevels = 12) {
    const map = new Map();
    for (const o of bids) {
      map.set(o.price, (map.get(o.price) || 0) + o.qty);
    }
    return [...map.entries()]
      .sort((a, b) => b[0] - a[0])
      .slice(0, maxLevels)
      .map(([price, qty]) => ({ price, qty }));
  }

  function getAggregatedAsks(maxLevels = 12) {
    const map = new Map();
    for (const o of asks) {
      map.set(o.price, (map.get(o.price) || 0) + o.qty);
    }
    return [...map.entries()]
      .sort((a, b) => a[0] - b[0])
      .slice(0, maxLevels)
      .map(([price, qty]) => ({ price, qty }));
  }

  function getSpread() {
    if (!bids.length || !asks.length) return null;
    return asks[0].price - bids[0].price;
  }

  function getMid() {
    if (!bids.length || !asks.length) return null;
    return (asks[0].price + bids[0].price) / 2;
  }

  return {
    bids, asks, executions, stats,
    addOrder, cancelOrder,
    getAggregatedBids, getAggregatedAsks,
    getSpread, getMid,
  };
}

// ─── Depth bar component ─────────────────────────────────────────────
function DepthBar({ price, qty, maxQty, side, onCancel, orders }) {
  const pct = maxQty > 0 ? (qty / maxQty) * 100 : 0;
  const isBid = side === "buy";

  return (
    <div style={{
      display: "flex", alignItems: "center", gap: 0,
      height: 32, position: "relative", marginBottom: 1,
      fontFamily: "'IBM Plex Mono', monospace",
    }}>
      {/* Price */}
      <div style={{
        width: 72, textAlign: "right", fontSize: 13, fontWeight: 600,
        color: isBid ? "#00e676" : "#ff1744",
        paddingRight: 10, flexShrink: 0,
        letterSpacing: "0.02em",
      }}>
        {price.toFixed(2)}
      </div>
      {/* Bar */}
      <div style={{
        flex: 1, position: "relative", height: "100%",
        display: "flex", alignItems: "center",
      }}>
        <div style={{
          position: "absolute",
          [isBid ? "right" : "left"]: 0,
          top: 1, bottom: 1,
          width: `${Math.max(pct, 2)}%`,
          background: isBid
            ? "linear-gradient(90deg, rgba(0,230,118,0.08), rgba(0,230,118,0.25))"
            : "linear-gradient(90deg, rgba(255,23,68,0.25), rgba(255,23,68,0.08))",
          borderRadius: 3,
          border: `1px solid ${isBid ? "rgba(0,230,118,0.3)" : "rgba(255,23,68,0.3)"}`,
          transition: "width 0.3s ease",
        }} />
        <span style={{
          position: "relative", zIndex: 1, fontSize: 12, color: "#8a94a6",
          [isBid ? "marginRight" : "marginLeft"]: "auto",
          [isBid ? "marginLeft" : "marginRight"]: "auto",
          padding: "0 8px",
        }}>
          {qty}
        </span>
      </div>
      {/* Qty label */}
      <div style={{
        width: 50, textAlign: "left", fontSize: 11, color: "#555e6e",
        paddingLeft: 8, flexShrink: 0,
      }}>
        {qty}
      </div>
    </div>
  );
}

// ─── Main App ────────────────────────────────────────────────────────
export default function OrderBookSimulator() {
  const engineRef = useRef(createEngine());
  const [, forceUpdate] = useState(0);
  const rerender = useCallback(() => forceUpdate((n) => n + 1), []);

  const [side, setSide] = useState("buy");
  const [price, setPrice] = useState("100.00");
  const [qty, setQty] = useState("10");
  const [cancelId, setCancelId] = useState("");
  const [flashExec, setFlashExec] = useState(null);
  const [log, setLog] = useState([]);

  const engine = engineRef.current;

  const addLog = (msg, type = "info") => {
    setLog((prev) => [{ msg, type, ts: Date.now() }, ...prev].slice(0, 40));
  };

  const handleAdd = () => {
    const p = parseFloat(price);
    const q = parseInt(qty, 10);
    if (isNaN(p) || isNaN(q) || q <= 0 || p <= 0) {
      addLog("Invalid price or quantity", "error");
      return;
    }
    const execsBefore = engine.executions.length;
    const { id } = engine.addOrder(side, p, q);
    const newExecs = engine.executions.length - execsBefore;
    addLog(`ADD ${side.toUpperCase()} #${id}  ${q} @ ${p.toFixed(2)}`, side === "buy" ? "buy" : "sell");
    if (newExecs > 0) {
      for (let i = 0; i < newExecs; i++) {
        const e = engine.executions[i];
        addLog(`  EXEC ${e.qty} @ ${e.price.toFixed(2)}  (aggr #${e.aggressor} ↔ rest #${e.passive})`, "exec");
      }
      setFlashExec(Date.now());
      setTimeout(() => setFlashExec(null), 600);
    }
    rerender();
  };

  const handleCancel = () => {
    const id = parseInt(cancelId, 10);
    if (isNaN(id)) { addLog("Enter a valid order ID", "error"); return; }
    if (engine.cancelOrder(id)) {
      addLog(`CANCEL #${id}`, "cancel");
    } else {
      addLog(`Order #${id} not found`, "error");
    }
    setCancelId("");
    rerender();
  };

  const seedBook = () => {
    nextId = 1;
    engineRef.current = createEngine();
    const e = engineRef.current;
    const prices = [97, 97.5, 98, 98.5, 99, 99.5];
    const askPrices = [100.5, 101, 101.5, 102, 102.5, 103];
    prices.forEach((p) => {
      const q = Math.floor(Math.random() * 40) + 5;
      e.addOrder("buy", p, q);
    });
    askPrices.forEach((p) => {
      const q = Math.floor(Math.random() * 40) + 5;
      e.addOrder("sell", p, q);
    });
    setLog([]);
    addLog("Book seeded with 12 orders", "info");
    rerender();
  };

  const clearBook = () => {
    nextId = 1;
    engineRef.current = createEngine();
    setLog([]);
    addLog("Book cleared", "info");
    rerender();
  };

  const aggBids = engine.getAggregatedBids(10);
  const aggAsks = engine.getAggregatedAsks(10);
  const allQtys = [...aggBids.map((l) => l.qty), ...aggAsks.map((l) => l.qty)];
  const maxQty = Math.max(...allQtys, 1);
  const spread = engine.getSpread();
  const mid = engine.getMid();

  return (
    <div style={{
      minHeight: "100vh", background: "#0b0e14",
      color: "#c5cdd8", fontFamily: "'IBM Plex Mono', 'Fira Code', monospace",
      padding: "20px 24px",
    }}>
      <link href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@300;400;500;600;700&family=Space+Grotesk:wght@400;600;700&display=swap" rel="stylesheet" />

      {/* Header */}
      <div style={{ marginBottom: 24, display: "flex", alignItems: "baseline", gap: 16, flexWrap: "wrap" }}>
        <h1 style={{
          fontSize: 22, fontWeight: 700, margin: 0,
          fontFamily: "'IBM Plex Mono', monospace",
          color: "#e8ecf1", letterSpacing: "-0.02em",
        }}>
          <span style={{ color: "#546e7a" }}>▸</span> ORDER BOOK ENGINE
        </h1>
        <span style={{ fontSize: 11, color: "#3e4a5a", letterSpacing: "0.08em" }}>
          PRICE-TIME PRIORITY · LIMIT ORDERS · LIVE MATCHING
        </span>
      </div>

      {/* Top bar: controls */}
      <div style={{
        display: "flex", gap: 8, marginBottom: 20, flexWrap: "wrap", alignItems: "center",
      }}>
        {/* Side toggle */}
        <div style={{ display: "flex", borderRadius: 6, overflow: "hidden", border: "1px solid #1e2636" }}>
          {["buy", "sell"].map((s) => (
            <button key={s} onClick={() => setSide(s)} style={{
              padding: "8px 18px", border: "none", cursor: "pointer",
              fontSize: 12, fontWeight: 600, fontFamily: "inherit",
              letterSpacing: "0.06em", textTransform: "uppercase",
              background: side === s
                ? (s === "buy" ? "rgba(0,230,118,0.15)" : "rgba(255,23,68,0.15)")
                : "#111722",
              color: side === s
                ? (s === "buy" ? "#00e676" : "#ff1744")
                : "#4a5568",
              transition: "all 0.2s",
            }}>
              {s}
            </button>
          ))}
        </div>

        {/* Price */}
        <div style={{ position: "relative" }}>
          <span style={{
            position: "absolute", left: 10, top: "50%", transform: "translateY(-50%)",
            fontSize: 10, color: "#3e4a5a", letterSpacing: "0.06em",
          }}>PRICE</span>
          <input value={price} onChange={(e) => setPrice(e.target.value)}
            style={{
              width: 120, padding: "8px 10px 8px 50px",
              background: "#111722", border: "1px solid #1e2636", borderRadius: 6,
              color: "#e8ecf1", fontSize: 14, fontFamily: "inherit",
              outline: "none",
            }}
            onKeyDown={(e) => e.key === "Enter" && handleAdd()}
          />
        </div>

        {/* Qty */}
        <div style={{ position: "relative" }}>
          <span style={{
            position: "absolute", left: 10, top: "50%", transform: "translateY(-50%)",
            fontSize: 10, color: "#3e4a5a", letterSpacing: "0.06em",
          }}>QTY</span>
          <input value={qty} onChange={(e) => setQty(e.target.value)}
            style={{
              width: 90, padding: "8px 10px 8px 40px",
              background: "#111722", border: "1px solid #1e2636", borderRadius: 6,
              color: "#e8ecf1", fontSize: 14, fontFamily: "inherit",
              outline: "none",
            }}
            onKeyDown={(e) => e.key === "Enter" && handleAdd()}
          />
        </div>

        {/* Submit */}
        <button onClick={handleAdd} style={{
          padding: "8px 22px", border: "none", borderRadius: 6, cursor: "pointer",
          fontSize: 12, fontWeight: 700, fontFamily: "inherit",
          letterSpacing: "0.06em",
          background: side === "buy"
            ? "linear-gradient(135deg, #00e676, #00c853)"
            : "linear-gradient(135deg, #ff1744, #d50000)",
          color: "#0b0e14",
          boxShadow: side === "buy"
            ? "0 0 20px rgba(0,230,118,0.2)"
            : "0 0 20px rgba(255,23,68,0.2)",
          transition: "all 0.2s",
        }}>
          SEND ORDER
        </button>

        <div style={{ width: 1, height: 28, background: "#1e2636", margin: "0 6px" }} />

        {/* Cancel */}
        <div style={{ position: "relative" }}>
          <span style={{
            position: "absolute", left: 10, top: "50%", transform: "translateY(-50%)",
            fontSize: 10, color: "#3e4a5a", letterSpacing: "0.06em",
          }}>ID#</span>
          <input value={cancelId} onChange={(e) => setCancelId(e.target.value)}
            placeholder=""
            style={{
              width: 60, padding: "8px 10px 8px 34px",
              background: "#111722", border: "1px solid #1e2636", borderRadius: 6,
              color: "#e8ecf1", fontSize: 14, fontFamily: "inherit",
              outline: "none",
            }}
            onKeyDown={(e) => e.key === "Enter" && handleCancel()}
          />
        </div>
        <button onClick={handleCancel} style={{
          padding: "8px 16px", border: "1px solid #ff9100", borderRadius: 6,
          cursor: "pointer", fontSize: 12, fontWeight: 600, fontFamily: "inherit",
          background: "rgba(255,145,0,0.08)", color: "#ff9100",
          letterSpacing: "0.04em",
        }}>
          CANCEL
        </button>

        <div style={{ width: 1, height: 28, background: "#1e2636", margin: "0 6px" }} />

        <button onClick={seedBook} style={{
          padding: "8px 14px", border: "1px solid #1e2636", borderRadius: 6,
          cursor: "pointer", fontSize: 11, fontFamily: "inherit",
          background: "#111722", color: "#8a94a6",
        }}>
          SEED BOOK
        </button>
        <button onClick={clearBook} style={{
          padding: "8px 14px", border: "1px solid #1e2636", borderRadius: 6,
          cursor: "pointer", fontSize: 11, fontFamily: "inherit",
          background: "#111722", color: "#8a94a6",
        }}>
          CLEAR
        </button>
      </div>

      {/* Main grid */}
      <div style={{ display: "grid", gridTemplateColumns: "1fr 320px", gap: 20 }}>

        {/* Left: Order Book Depth */}
        <div style={{
          background: "#0f1319", border: "1px solid #1a2030",
          borderRadius: 10, padding: 20,
          boxShadow: flashExec ? "0 0 40px rgba(255,193,7,0.08)" : "none",
          transition: "box-shadow 0.3s",
        }}>
          {/* Stats bar */}
          <div style={{
            display: "flex", gap: 24, marginBottom: 16,
            fontSize: 11, color: "#546e7a", letterSpacing: "0.06em",
          }}>
            <span>SPREAD <span style={{ color: spread !== null ? "#ffc107" : "#333" }}>
              {spread !== null ? spread.toFixed(2) : "—"}
            </span></span>
            <span>MID <span style={{ color: mid !== null ? "#80cbc4" : "#333" }}>
              {mid !== null ? mid.toFixed(2) : "—"}
            </span></span>
            <span>BIDS <span style={{ color: "#00e676" }}>{aggBids.length}</span></span>
            <span>ASKS <span style={{ color: "#ff1744" }}>{aggAsks.length}</span></span>
            <span>RESTING <span style={{ color: "#8a94a6" }}>{engine.bids.length + engine.asks.length}</span></span>
          </div>

          {/* Column headers */}
          <div style={{
            display: "flex", fontSize: 10, color: "#3e4a5a",
            letterSpacing: "0.1em", marginBottom: 6, padding: "0 0 4px 0",
            borderBottom: "1px solid #151c28",
          }}>
            <div style={{ width: 72, textAlign: "right", paddingRight: 10 }}>PRICE</div>
            <div style={{ flex: 1, textAlign: "center" }}>DEPTH</div>
            <div style={{ width: 50, paddingLeft: 8 }}>QTY</div>
          </div>

          {/* Ask side (reversed so worst ask on top, best ask near spread) */}
          <div style={{ marginBottom: 2 }}>
            {aggAsks.length === 0 ? (
              <div style={{ textAlign: "center", padding: 16, color: "#2a3140", fontSize: 12 }}>
                no asks
              </div>
            ) : (
              [...aggAsks].reverse().map((l) => (
                <DepthBar key={`a-${l.price}`} price={l.price} qty={l.qty} maxQty={maxQty} side="sell" />
              ))
            )}
          </div>

          {/* Spread divider */}
          <div style={{
            display: "flex", alignItems: "center", gap: 12,
            margin: "6px 0", padding: "6px 0",
          }}>
            <div style={{ flex: 1, height: 1, background: "linear-gradient(90deg, transparent, #1e2636, transparent)" }} />
            <span style={{
              fontSize: 10, letterSpacing: "0.12em", color: "#3e4a5a",
              padding: "3px 12px",
              border: "1px solid #1e2636", borderRadius: 20,
              background: "#0b0e14",
            }}>
              {spread !== null ? `SPREAD ${spread.toFixed(2)}` : "NO SPREAD"}
            </span>
            <div style={{ flex: 1, height: 1, background: "linear-gradient(90deg, transparent, #1e2636, transparent)" }} />
          </div>

          {/* Bid side */}
          <div>
            {aggBids.length === 0 ? (
              <div style={{ textAlign: "center", padding: 16, color: "#2a3140", fontSize: 12 }}>
                no bids
              </div>
            ) : (
              aggBids.map((l) => (
                <DepthBar key={`b-${l.price}`} price={l.price} qty={l.qty} maxQty={maxQty} side="buy" />
              ))
            )}
          </div>

          {/* Individual orders */}
          {(engine.bids.length + engine.asks.length > 0) && (
            <div style={{ marginTop: 20 }}>
              <div style={{
                fontSize: 10, color: "#3e4a5a", letterSpacing: "0.1em",
                marginBottom: 8, borderBottom: "1px solid #151c28", paddingBottom: 4,
              }}>
                RESTING ORDERS (click ID to cancel)
              </div>
              <div style={{ display: "flex", flexWrap: "wrap", gap: 4 }}>
                {[...engine.asks].reverse().concat(engine.bids).map((o) => (
                  <button
                    key={o.id}
                    onClick={() => {
                      engine.cancelOrder(o.id);
                      addLog(`CANCEL #${o.id}`, "cancel");
                      rerender();
                    }}
                    style={{
                      padding: "3px 8px", border: "none", borderRadius: 4,
                      cursor: "pointer", fontSize: 10, fontFamily: "inherit",
                      background: o.side === "buy" ? "rgba(0,230,118,0.08)" : "rgba(255,23,68,0.08)",
                      color: o.side === "buy" ? "#00e676" : "#ff1744",
                      transition: "all 0.15s",
                    }}
                    title={`${o.side.toUpperCase()} ${o.qty}@${o.price.toFixed(2)}`}
                  >
                    #{o.id} {o.qty}@{o.price.toFixed(2)}
                  </button>
                ))}
              </div>
            </div>
          )}
        </div>

        {/* Right: Event log + explanation */}
        <div style={{ display: "flex", flexDirection: "column", gap: 16 }}>
          {/* Event tape */}
          <div style={{
            background: "#0f1319", border: "1px solid #1a2030",
            borderRadius: 10, padding: 16, flex: 1, minHeight: 200,
            maxHeight: 420, overflow: "hidden",
          }}>
            <div style={{
              fontSize: 10, color: "#3e4a5a", letterSpacing: "0.1em",
              marginBottom: 10, borderBottom: "1px solid #151c28", paddingBottom: 4,
            }}>
              EVENT LOG
            </div>
            <div style={{ overflow: "auto", maxHeight: 360 }}>
              {log.length === 0 ? (
                <div style={{ color: "#2a3140", fontSize: 12, padding: 8 }}>
                  Click "SEED BOOK" to start, then send orders...
                </div>
              ) : (
                log.map((entry, i) => (
                  <div key={`${entry.ts}-${i}`} style={{
                    fontSize: 11, padding: "3px 0",
                    color: entry.type === "buy" ? "#00e676"
                         : entry.type === "sell" ? "#ff1744"
                         : entry.type === "exec" ? "#ffc107"
                         : entry.type === "cancel" ? "#ff9100"
                         : entry.type === "error" ? "#e57373"
                         : "#546e7a",
                    borderLeft: entry.type === "exec" ? "2px solid #ffc107" : "2px solid transparent",
                    paddingLeft: 8,
                    opacity: i > 20 ? 0.4 : 1,
                  }}>
                    {entry.msg}
                  </div>
                ))
              )}
            </div>
          </div>

          {/* How it works panel */}
          <div style={{
            background: "#0f1319", border: "1px solid #1a2030",
            borderRadius: 10, padding: 16, fontSize: 11, lineHeight: 1.7,
            color: "#546e7a",
          }}>
            <div style={{
              fontSize: 10, letterSpacing: "0.1em", marginBottom: 8,
              borderBottom: "1px solid #151c28", paddingBottom: 4, color: "#3e4a5a",
            }}>
              HOW IT WORKS
            </div>
            <p style={{ margin: "0 0 6px" }}>
              <span style={{ color: "#80cbc4" }}>Price-time priority:</span> best
              price fills first. At the same price, earliest order wins.
            </p>
            <p style={{ margin: "0 0 6px" }}>
              <span style={{ color: "#80cbc4" }}>Crossing:</span> a BUY at 101 with
              an ASK at 100 triggers an immediate execution at the resting
              price (100).
            </p>
            <p style={{ margin: "0 0 6px" }}>
              <span style={{ color: "#80cbc4" }}>Spread:</span> gap between the
              best bid and best ask. Tighter = more liquid.
            </p>
            <p style={{ margin: 0 }}>
              <span style={{ color: "#80cbc4" }}>Cancel:</span> removes a resting
              order instantly (O(1) in the C++ engine via intrusive list).
            </p>
          </div>
        </div>
      </div>
    </div>
  );
}
