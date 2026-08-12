// BreezeLink SSR admin: sidebar toggle + realtime refresh.
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

// Vá DOM tại chỗ thay vì thay cả khối #main.
//
// TRƯỚC ĐÂY LÀ `main.innerHTML = fresh.innerHTML`, và nó đẻ ra ba lỗi mà mỗi
// lỗi từng được vá riêng một lần:
//
//   1. NHÁY. ds/animations/cards.css cho mọi `.card-grid > .card` chạy
//      `card-enter` (mờ 0 -> 1, trượt lên 20px). Thay DOM là huỷ và tạo lại
//      từng thẻ, nên animation vào-trang chạy lại — cả bảng chớp một cái mỗi
//      lần có số liệu mới, tức là vài giây một lần.
//   2. Trạng thái gập/mở của <details> biến mất theo phần tử cũ.
//   3. Chữ đang gõ dở trong form bị xoá.
//
// Ba cái cùng một gốc: PHÁ HUỶ PHẦN TỬ. Vá tại chỗ giải quyết cả ba, và kèm
// theo giữ luôn vị trí cuộn, ô đang focus, vùng bôi đen và nội dung canvas —
// mỗi thứ đó nếu đi tiếp đường cũ lại cần thêm một bản vá nữa.
//
// CÁCH LÀM: đi song song hai cây. Nút cùng loại và cùng tên thẻ thì đi sâu vào
// trong và chỉ sửa chỗ khác; khác nhau mới thay nút đó. Trên trang này gần như
// mọi thứ đều khớp, nên thực tế chỉ vài nút VĂN BẢN chứa nhiệt độ bị chạm tới,
// và trình duyệt chỉ vẽ lại đúng chỗ đó.


// Chọn giao diện sáng/tối (trang /web/settings).
//
// Việc ÁP DỤNG giao diện đã xong từ trước — base.html chạy một script nội tuyến
// trong <head> để đặt data-theme trước lần vẽ đầu tiên. Ở đây chỉ lo phần đổi
// lựa chọn: ghi vào localStorage, đặt lại thuộc tính, và đánh dấu nút đang chọn.
//
// GẮN LẠI SAU MỖI LẦN THAY #main. Trang cài đặt nằm trong #main, mà mỗi lần đẩy
// WebSocket là cả khối đó bị thay mới — listener gắn vào nút cũ chết theo. Uỷ
// quyền lên document thì chỉ gắn một lần và sống mãi.
var acTheme = (function () {
  var KEY = "ac_theme";

  function saved() {
    try { return localStorage.getItem(KEY) || "auto"; } catch (e) { return "auto"; }
  }

  function apply(choice) {
    var effective = choice;
    if (choice === "auto") {
      effective = window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
    }
    document.documentElement.setAttribute("data-theme", effective);
    // Đánh dấu nút nào đang chọn — theo LỰA CHỌN chứ không theo giao diện đang
    // hiện. Chọn "Theo hệ thống" lúc máy đang tối thì phải sáng ô "Theo hệ
    // thống", không phải ô "Tối".
    var opts = document.querySelectorAll(".ac-theme-opt");
    for (var i = 0; i < opts.length; i++) {
      opts[i].setAttribute("aria-checked",
        opts[i].getAttribute("data-theme") === choice ? "true" : "false");
    }
  }

  document.addEventListener("click", function (e) {
    var btn = e.target.closest ? e.target.closest(".ac-theme-opt") : null;
    if (!btn) return;
    var choice = btn.getAttribute("data-theme");
    try { localStorage.setItem(KEY, choice); } catch (err) {}
    apply(choice);
  });

  // Máy đổi sáng/tối trong lúc trang đang mở (hẹn giờ ban đêm của hệ điều hành)
  // thì trang phải theo — nhưng chỉ khi người dùng để "auto".
  try {
    window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", function () {
      if (saved() === "auto") apply("auto");
    });
  } catch (e) {}

  document.addEventListener("DOMContentLoaded", function () { apply(saved()); });
  return { apply: apply, saved: saved };
})();

// Chặn việc làm mới trực tiếp ghi đè lên thứ người dùng đang gõ dở.
//
// VẤN ĐỀ: mỗi lần đẩy WebSocket đều làm `main.innerHTML = ...`. Trên trang khách
// hàng có form Cấu hình thuật toán, nên đang sửa "Vùng trễ" mà node gửi số liệu
// về là ô đó bị thay bằng giá trị cũ của máy chủ — chữ vừa gõ biến mất, không
// báo gì. Node gửi mỗi vài giây nên gần như KHÔNG THỂ sửa xong một ô.
//
// DỪNG HẲN VIỆC LÀM MỚI, không cố khôi phục từng ô sau khi thay. Khôi phục nghe
// có vẻ hay hơn nhưng nó trộn số của máy chủ với số người dùng đang gõ trong
// cùng một form, và không ai nhìn vào biết ô nào thuộc bên nào. Trang đứng yên
// vài chục giây trong lúc sửa là cái giá đúng.
//
// Bắt ở pha CAPTURE trên document: form được vẽ lại liên tục, nên gắn listener
// vào từng ô là gắn vào những phần tử sắp bị vứt đi.
var acFormGuard = (function () {
  var dirty = false;
  function touch(e) {
    var t = e.target;
    if (!t) return;
    var tag = t.tagName;
    if (tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT") dirty = true;
  }
  document.addEventListener("input", touch, true);
  document.addEventListener("change", touch, true);
  return {
    isDirty: function () { return dirty; },
    clear: function () { dirty = false; }
  };
})();

var acMorph = (function () {
  // Thuộc tính KHÔNG đồng bộ vì chúng là trạng thái của NGƯỜI DÙNG, không phải
  // của máy chủ. `open` là cái quan trọng nhất: máy chủ luôn render nhóm
  // toàn-ngoại-tuyến ở trạng thái mở, nên đồng bộ nó sẽ bung lại đúng cái nhóm
  // mà người dùng vừa cố ý đóng.
  var SKIP = { open: 1 };

  function syncAttrs(from, to) {
    var i, a;
    for (i = to.attributes.length - 1; i >= 0; i--) {
      a = to.attributes[i];
      if (SKIP[a.name]) continue;
      if (from.getAttribute(a.name) !== a.value) from.setAttribute(a.name, a.value);
    }
    // Duyệt NGƯỢC khi xoá: removeAttribute làm danh sách co lại ngay lập tức,
    // nên duyệt xuôi sẽ nhảy cóc qua thuộc tính kế tiếp.
    for (i = from.attributes.length - 1; i >= 0; i--) {
      a = from.attributes[i];
      if (SKIP[a.name]) continue;
      if (!to.hasAttribute(a.name)) from.removeAttribute(a.name);
    }
  }

  function same(a, b) {
    return a.nodeType === b.nodeType && a.nodeName === b.nodeName;
  }

  function morph(from, to) {
    if (from.nodeType === 3 || from.nodeType === 8) {   // văn bản / chú thích
      if (from.nodeValue !== to.nodeValue) from.nodeValue = to.nodeValue;
      return;
    }
    if (from.nodeType !== 1) return;
    syncAttrs(from, to);

    // KHÔNG đi vào trong ô nhập. Thuộc tính `value` chỉ là giá trị MẶC ĐỊNH;
    // sau khi người dùng gõ, trình duyệt giữ giá trị thật ở thuộc tính JS chứ
    // không ở HTML. Trên trang này ô nhập là cấu hình do người dùng sửa, không
    // phải số liệu trực tiếp, nên máy chủ không có gì mới để nói về chúng.
    var tag = from.nodeName;
    if (tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT") return;

    var a = from.firstChild, b = to.firstChild, nextA, nextB;
    while (a || b) {
      // Lấy nút kế TRƯỚC khi đụng vào cây, vì mọi thao tác dưới đây đều làm
      // nextSibling của nút hiện tại đổi nghĩa.
      if (!b) { nextA = a.nextSibling; from.removeChild(a); a = nextA; continue; }
      if (!a) { nextB = b.nextSibling; from.appendChild(b); b = nextB; continue; }
      nextA = a.nextSibling;
      nextB = b.nextSibling;
      if (same(a, b)) {
        morph(a, b);
      } else {
        from.replaceChild(b, a);
      }
      a = nextA;
      b = nextB;
    }
  }

  return { apply: morph };
})();

(function () {
  // Rail collapse/expand (DESKTOP), persisted so base.html's inline <head>
  // script can apply it before first paint (no flash of the wrong rail width).
  var btn = document.getElementById("acRailToggle");
  if (!btn) return;
  btn.addEventListener("click", function () {
    var on = document.documentElement.classList.toggle("rail-collapsed");
    try { localStorage.setItem("ac_rail_collapsed", on ? "1" : "0"); } catch (e) {}
  });
})();

(function () {
  // Mobile drawer: the hamburger toggles html.rail-open (rail slides in over a
  // dimmed backdrop). Tapping the backdrop, a nav item, or Escape closes it.
  var burger = document.getElementById("acBurger");
  var backdrop = document.getElementById("acRailBackdrop");
  var rail = document.getElementById("acRail");
  if (!burger || !backdrop || !rail) return;
  var root = document.documentElement;
  function close() { root.classList.remove("rail-open"); }
  burger.addEventListener("click", function () { root.classList.toggle("rail-open"); });
  backdrop.addEventListener("click", close);
  document.addEventListener("keydown", function (e) { if (e.key === "Escape") close(); });
  rail.querySelectorAll(".ac-rail__item").forEach(function (a) {
    a.addEventListener("click", close);
  });
})();

(function () {
  var main = document.getElementById("main");
  // LOGGED-IN SHELL ONLY (guarded by #acRail, which base.html renders only when
  // there's a user). The live feed's auto-refetch does fetch(location.href) and
  // swaps #main — fine on the dashboard, but on the PUBLIC reset-password page
  // that URL has lost its ?token after the POST, so the refetch rendered the
  // "Liên kết thiếu mã đặt lại" error OVER the success tick. A public page has
  // no live state to push anyway, so skip the whole feed there.
  if (!main || !document.getElementById("acRail") || !("WebSocket" in window)) return;

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
    // KIỂM LẠI Ở ĐÂY, không chỉ ở refresh(). Một lần làm mới được hẹn giờ TRƯỚC
    // khi người dùng chạm vào ô nhập vẫn sẽ nổ sau đúng MIN_GAP_MS — và nó xoá
    // mất chữ vừa gõ. Đây là chốt cuối, ngay trước lệnh ghi đè.
    if (acFormGuard.isDirty()) return;
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
    acMorph.apply(main, fresh);
    // Nút chọn giao diện: máy chủ render chúng ở trạng thái chưa chọn vì nó
    // không biết lựa chọn nằm trong localStorage của trình duyệt.
    acTheme.apply(acTheme.saved());
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
    // Đang có chữ gõ dở trong một form: thà để trang cũ vài chục giây còn hơn
    // xoá mất thứ người dùng vừa gõ. Mở lại ngay khi lưu xong.
    if (acFormGuard.isDirty()) return;
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
//   data-ajax="redirect"  -> show the tick, then GO to where the server sent us
//                            (for deletes: the page you were on no longer exists)
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
    var doc = new DOMParser().parseFromString(html, "text/html");
    var fresh = doc.getElementById("main");
    if (!fresh) return;
    // Vá tại chỗ nên trang không nhảy về đầu — không cần nhớ scrollY nữa.
    main.classList.remove("page-content-stagger");
    acMorph.apply(main, fresh);
    acTheme.apply(acTheme.saved());

    // The header sits OUTSIDE #main, so a save that changes it (uploading or
    // removing your avatar) would leave the old picture in the top bar. The
    // response we already parsed carries the freshly rendered header, and the
    // <img> in it has a new ?v=avatar_updated_at — which matters because the
    // avatar is served with max-age=86400, so without swapping the src the
    // browser would keep showing the cached OLD image for a day.
    var head = document.querySelector(".header-actions");
    var freshHead = doc.querySelector(".header-actions");
    if (head && freshHead) head.innerHTML = freshHead.innerHTML;
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
          // Đã lưu xong: những gì trên màn hình giờ đúng bằng những gì máy chủ
          // giữ, nên không còn gì để bảo vệ — mở lại luồng cập nhật trực tiếp.
          acFormGuard.clear();
          if (mode === "refresh") {
            swapMain(text); // the response IS the fresh page
          } else if (mode === "redirect" && url) {
            // The record this page was showing is gone, so staying here (even
            // with swapped content) leaves the address bar pointing at a row
            // that 404s on reload. Follow wherever the server redirected to,
            // after a beat so the success tick is actually seen.
            setTimeout(function () { location.href = url.href; }, 900);
          }
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
