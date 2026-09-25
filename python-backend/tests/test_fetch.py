"""URL import against a local server that blocks non-browser clients the way job boards do."""

from __future__ import annotations

import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from seek.engines import jobs  # noqa: E402

PAGE = "<html><body><h1>Warehouse Associate</h1><p>Pick – pack • ship, don’t stop.</p></body></html>"


class _BoardHandler(BaseHTTPRequestHandler):
    def do_GET(self):  # noqa: N802
        ua = self.headers.get("User-Agent", "")
        # Typical WAF rule: an unknown product token or missing navigation headers means "bot".
        looks_like_browser = ("Chrome/" in ua and "SEEK/" not in ua and "python-requests" not in ua
                              and self.headers.get("Sec-Fetch-Mode") == "navigate")
        if self.path == "/always-blocked" or not looks_like_browser:
            self.send_response(403)
            self.end_headers()
            self.wfile.write(b"Access denied")
            return
        body = PAGE.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html")  # no charset, like many boards
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


@pytest.fixture()
def board():
    server = ThreadingHTTPServer(("127.0.0.1", 0), _BoardHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    yield f"http://127.0.0.1:{server.server_address[1]}"
    server.shutdown()
    server.server_close()


def test_old_headers_were_blocked(board):
    import requests
    old = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
                         "Chrome/126.0 Safari/537.36 SEEK/2.0",
           "Accept-Language": "en-US,en;q=0.9", "Accept": "text/html,application/xhtml+xml"}
    assert requests.get(board + "/job", headers=old, timeout=5).status_code == 403


def test_plain_requests_passes_as_browser(board, monkeypatch):
    monkeypatch.setattr(jobs, "_has_curl_cffi", lambda: False)
    page, final_url = jobs.fetch_url(board + "/job")
    assert "Warehouse Associate" in page
    assert "– pack • ship, don’t" in page  # UTF-8 without a charset header isn't mojibake
    assert final_url == board + "/job"


def test_impersonated_client(board):
    pytest.importorskip("curl_cffi")
    page, _ = jobs.fetch_url(board + "/job")
    assert "Warehouse Associate" in page


def test_falls_back_when_impersonation_fails(board, monkeypatch):
    real_open = jobs._open

    def flaky_open(url, impersonate):
        if impersonate:
            raise OSError("curl: (35) TLS handshake failed")
        return real_open(url, impersonate)

    monkeypatch.setattr(jobs, "_has_curl_cffi", lambda: True)
    monkeypatch.setattr(jobs, "_open", flaky_open)
    page, _ = jobs.fetch_url(board + "/job")
    assert "Warehouse Associate" in page


def test_blocked_everywhere_reports_block_not_network(board, monkeypatch):
    real_open = jobs._open

    def open_then_fail(url, impersonate):
        if impersonate:
            return real_open(url, False)  # gets the 403
        raise OSError("connection reset")

    monkeypatch.setattr(jobs, "_has_curl_cffi", lambda: True)
    monkeypatch.setattr(jobs, "_open", open_then_fail)
    with pytest.raises(jobs.FetchError, match=r"blocked automatic reading \(HTTP 403\)"):
        jobs.fetch_url(board + "/always-blocked")


def test_block_message_suggests_curl_cffi_when_missing(board, monkeypatch):
    monkeypatch.setattr(jobs, "_has_curl_cffi", lambda: False)
    with pytest.raises(jobs.FetchError, match="pip install curl_cffi"):
        jobs.fetch_url(board + "/always-blocked")
