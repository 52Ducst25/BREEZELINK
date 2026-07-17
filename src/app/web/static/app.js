// Aircon SSR admin: sidebar toggle + realtime refresh.
//
// Realtime design choice: on every non-ping WebSocket message we RE-FETCH
// the current page and swap #main (SafeKitchen's pattern, design (a)),
// rather than (b) patching the DOM directly from the pushed JSON snapshot.
// Chosen because every bit of comfort-pipeline rendering logic (em-dash for
// missing data, thermal-state -> colour mapping, Vietnamese labels) already
// lives once in Python/Jinja (web/dashboard_view.py, the templates). Option
// (b) would require re-implementing that formatting a second time in JS,
// which is exactly how the project's earlier "0.0 fabricated from empty
// telemetry" bug happened — two renderers of the same decision drifting
// apart. One extra request per update is a cheap price for a single source
// of truth.
//
// Also unlike SafeKitchen: our feed is ONE org-scoped socket at
// /api/v1/ws/live (not one per device), and cookie-authenticated — the
// browser sends the httpOnly session cookie on the handshake automatically
// (same-origin), no ?token= needed (see api/v1/ws_routes.py).

(function () {
  // Rail collapse/expand, persisted so base.html's inline <head> script can
  // apply it before first paint (no flash of the wrong rail width).
  var btn = document.getElementById("acRailToggle");
  if (!btn) return;
  btn.addEventListener("click", function () {
    var on = document.documentElement.classList.toggle("rail-collapsed");
    try { localStorage.setItem("ac_rail_collapsed", on ? "1" : "0"); } catch (e) {}
  });
})();

(function () {
  var main = document.getElementById("main");
  if (!main || !("WebSocket" in window)) return;

  var liveDot = document.getElementById("acLiveDot");
  function setLive(on) {
    if (liveDot) liveDot.classList.toggle("is-live", on);
  }

  var proto = location.protocol === "https:" ? "wss:" : "ws:";
  var pending = null;
  var lastAt = 0;
  var missedWhileHidden = false;

  // Rate limit, not just burst-coalescing. Telemetry lands roughly every few
  // seconds per node; at the old 800ms debounce every single reading turned
  // into its own fetch + full #main replace, so the page visibly rebuilt
  // itself nonstop. MIN_GAP_MS is the floor between two refreshes: bursts
  // collapse into one, and a steady stream still costs at most one refresh
  // per window. Trailing-edge, so the last state always wins.
  var MIN_GAP_MS = 5000;

  function swap(html) {
    var doc = new DOMParser().parseFromString(html, "text/html");
    var fresh = doc.getElementById("main");
    if (!fresh) return;
    // Strip the entrance animation from the LIVE #main -- not from `fresh`.
    // The class sits ON #main, and querySelectorAll only walks DESCENDANTS,
    // so the old `fresh.querySelectorAll(".page-content-stagger")` matched
    // nothing and (since only innerHTML is copied) the live element kept the
    // class regardless. Result: ".page-content-stagger > *" re-fired
    // content-reveal on every freshly inserted child, i.e. the whole page
    // replayed its intro on every push. Removed once, here, because the
    // entrance is a one-time page-load effect by definition.
    main.classList.remove("page-content-stagger");
    main.innerHTML = fresh.innerHTML;
    lastAt = Date.now();
  }

  function fetchNow() {
    return fetch(location.href, { cache: "no-store", credentials: "same-origin" })
      .then(function (r) {
        if (r.redirected) { location.reload(); return null; } // session expired -> /web/login
        return r.text();
      })
      .then(function (html) { if (html) swap(html); })
      .catch(function () {});
  }

  function refresh() {
    // A hidden tab still receives pushes; refreshing it burns the server's
    // time to paint pixels nobody sees. Remember instead, and catch up once
    // on the way back.
    if (document.hidden) { missedWhileHidden = true; return; }
    if (pending) return; // already scheduled -- this push folds into it
    var wait = Math.max(0, MIN_GAP_MS - (Date.now() - lastAt));
    pending = setTimeout(function () {
      pending = null;
      fetchNow();
    }, wait);
  }

  document.addEventListener("visibilitychange", function () {
    if (!document.hidden && missedWhileHidden) {
      missedWhileHidden = false;
      refresh();
    }
  });

  function connect() {
    var ws;
    try {
      ws = new WebSocket(proto + "//" + location.host + "/api/v1/ws/live");
    } catch (e) {
      return;
    }
    ws.onopen = function () { setLive(true); };
    ws.onmessage = function (evt) {
      try {
        var msg = JSON.parse(evt.data);
        if (msg && msg.type === "ping") return; // heartbeat only -- nothing changed
      } catch (e) {
        // non-JSON payload: fall through and refresh anyway, just in case
      }
      refresh();
    };
    ws.onclose = function () {
      setLive(false);
      setTimeout(connect, 4000); // auto-reconnect
    };
    ws.onerror = function () {
      try { ws.close(); } catch (e) {}
    };
  }
  connect();
})();

// AJAX form save: submit forms marked data-ajax without a full-page reload,
// then show a centred green check. Progressive enhancement — a form with no
// data-ajax, or the page with JS off, still POSTs and redirects normally.
//
// Not WebSocket: a save is a request → response, which fetch models directly.
// The WebSocket above is for the OPPOSITE direction (server pushing live state
// to the page); routing a save through it would bypass the normal HTTP route,
// its auth and its form parsing for no gain.
//
//   data-ajax="save"      -> just confirm success (values already on screen)
//   data-ajax="refresh"   -> also swap #main from the response (a list changed)
//   data-ajax-msg="..."    -> success text (default "Đã lưu thành công")
//   data-ajax-pending="…" -> hold a spinner while the request runs (for a slow
//                            upload like a 50MB APK, so it doesn't look frozen)
//   data-ajax-confirm="…" -> ask first, in an in-app Yes/No box (no native
//                            confirm() dialog), then submit on Yes
(function () {
  function makeToast(kind, msg) {
    // kind: "ok" | "err" | "pending"
    var el = document.createElement("div");
    el.className = "ac-toast" + (kind === "err" ? " ac-toast--err" : "");
    var visual;
    if (kind === "pending") {
      visual = '<span class="ac-toast__spin" aria-hidden="true"></span>';
    } else {
      var icon = kind === "ok"
        ? '<circle cx="26" cy="26" r="24"/><path d="M15 27l7 7 15-15"/>'
        : '<circle cx="26" cy="26" r="24"/><path d="M18 18l16 16M34 18l-16 16"/>';
      visual = '<svg class="ac-toast__ico" viewBox="0 0 52 52" aria-hidden="true">' + icon + "</svg>";
    }
    el.innerHTML =
      '<div class="ac-toast__box" role="status" aria-live="polite">' + visual +
      '<span class="ac-toast__txt"></span></div>';
    el.querySelector(".ac-toast__txt").textContent = msg; // textContent = no HTML injection
    document.body.appendChild(el);
    requestAnimationFrame(function () { el.classList.add("is-in"); });
    return el;
  }
  function removeToast(el) {
    if (!el) return;
    el.classList.remove("is-in");
    setTimeout(function () { el.remove(); }, 250);
  }
  function toast(ok, msg) {
    var el = makeToast(ok ? "ok" : "err", msg);
    setTimeout(function () { removeToast(el); }, ok ? 1300 : 2800);
  }

  function swapMain(html) {
    var main = document.getElementById("main");
    if (!main) return;
    var fresh = new DOMParser().parseFromString(html, "text/html").getElementById("main");
    if (!fresh) return;
    var y = window.scrollY; // keep the viewport where it was, not scrolled to top
    main.classList.remove("page-content-stagger");
    main.innerHTML = fresh.innerHTML;
    window.scrollTo(0, y);
  }

  // In-app Yes/No box, styled like the toast — replaces the browser's native
  // confirm() dialog (the ugly "admin.vi-du.com says…" one). Resolves true on
  // confirm, false on cancel / Esc / backdrop click.
  function confirmBox(msg) {
    return new Promise(function (resolve) {
      var ov = document.createElement("div");
      ov.className = "ac-confirm";
      ov.innerHTML =
        '<div class="ac-confirm__box" role="dialog" aria-modal="true">' +
        '<svg class="ac-confirm__ico" viewBox="0 0 52 52" aria-hidden="true">' +
        '<path d="M26 5 3 46h46z"/><path d="M26 21v11"/><circle cx="26" cy="39" r="1.6"/></svg>' +
        '<p class="ac-confirm__msg"></p>' +
        '<div class="ac-confirm__actions">' +
        '<button type="button" class="btn btn-secondary ac-confirm__no">Huỷ</button>' +
        '<button type="button" class="btn btn-danger ac-confirm__yes">Xoá</button>' +
        "</div></div>";
      ov.querySelector(".ac-confirm__msg").textContent = msg; // no HTML injection
      document.body.appendChild(ov);
      requestAnimationFrame(function () { ov.classList.add("is-in"); });

      function close(val) {
        ov.classList.remove("is-in");
        setTimeout(function () { ov.remove(); }, 200);
        document.removeEventListener("keydown", onKey);
        resolve(val);
      }
      function onKey(ev) {
        if (ev.key === "Escape") close(false);
        else if (ev.key === "Enter") close(true);
      }
      ov.querySelector(".ac-confirm__no").addEventListener("click", function () { close(false); });
      ov.querySelector(".ac-confirm__yes").addEventListener("click", function () { close(true); });
      ov.addEventListener("click", function (ev) { if (ev.target === ov) close(false); });
      document.addEventListener("keydown", onKey);
      ov.querySelector(".ac-confirm__no").focus(); // default to the safe choice
    });
  }

  function submitForm(form) {
    var mode = form.getAttribute("data-ajax");
    var okMsg = form.getAttribute("data-ajax-msg") || "Đã lưu thành công";
    var pendingMsg = form.getAttribute("data-ajax-pending");
    var pendingEl = pendingMsg ? makeToast("pending", pendingMsg) : null;
    var btns = form.querySelectorAll("button[type=submit], button:not([type])");
    btns.forEach(function (b) { b.disabled = true; });

    fetch(form.action || location.href, {
      method: (form.method || "post").toUpperCase(),
      body: new FormData(form), // includes inputs linked via the form= attribute
      credentials: "same-origin",
      headers: { "X-Requested-With": "fetch" },
    })
      .then(function (r) {
        return r.text().then(function (text) {
          removeToast(pendingEl); // clear the spinner FIRST, so it never overlaps the result
          var url;
          try { url = new URL(r.url); } catch (_) { url = null; }
          if (url && url.pathname === "/web/login") { location.reload(); return; } // session gone
          // The PRG routes redirect to ?err=<message> on failure; the fetch
          // follows that, so the final URL's err param IS the error — no
          // backend change needed to tell success from failure.
          var err = url && url.searchParams.get("err");
          if (err) { toast(false, err); return; }
          toast(true, okMsg);
          if (mode === "refresh") swapMain(text); // the response IS the fresh page
        });
      })
      .catch(function () { removeToast(pendingEl); toast(false, "Lỗi kết nối — thử lại"); })
      .finally(function () { btns.forEach(function (b) { b.disabled = false; }); });
  }

  document.addEventListener("submit", function (e) {
    var form = e.target;
    if (!(form instanceof HTMLFormElement) || !form.hasAttribute("data-ajax")) return;
    if (e.defaultPrevented) return; // some other handler already cancelled it
    e.preventDefault();

    var ask = form.getAttribute("data-ajax-confirm");
    if (ask) {
      confirmBox(ask).then(function (ok) { if (ok) submitForm(form); });
    } else {
      submitForm(form);
    }
  });
})();
