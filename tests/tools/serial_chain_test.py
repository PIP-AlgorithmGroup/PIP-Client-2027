"""管理面冲突行为回归；不启动MockServer或打开实体串口。"""

import io
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import urllib.error

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from test_serial_chain import MockSession


class MockSessionTest(unittest.TestCase):
    """仅明确未受理且控制状态未变的冲突可重新提交，不能重复已受理操作。"""

    def test_revision_only_conflict_refreshes_once_with_same_key(self):
        snapshot = dict(session_id="test", revision="1", modules=[dict(id="bridge", state="ready")])
        current = dict(snapshot, revision="2")
        conflict = urllib.error.HTTPError(
            "http://test", 409, "Conflict", {}, io.BytesIO(b'{"error":"revision_conflict"}')
        )
        response = io.BytesIO(b'{"request_id":"request"}')
        with patch.object(
            MockSession, "get", side_effect=[snapshot, current, {"state": "completed"}]
        ), patch("urllib.request.urlopen", side_effect=[conflict, response]) as request:
            MockSession(1).execute("bridge", "stop")
        first, second = [json.loads(call.args[0].data) for call in request.call_args_list]
        self.assertEqual(second, dict(first, expected_revision="2"))

    def test_control_state_change_refuses_resubmission(self):
        snapshot = dict(session_id="test", revision="1", modules=[dict(id="bridge", state="ready")])
        current = dict(snapshot, revision="2", modules=[dict(id="bridge", state="error")])
        conflict = urllib.error.HTTPError(
            "http://test", 409, "Conflict", {}, io.BytesIO(b'{"error":"revision_conflict"}')
        )
        with patch.object(MockSession, "get", side_effect=[snapshot, current]), patch(
            "urllib.request.urlopen", side_effect=conflict
        ) as request:
            with self.assertRaisesRegex(RuntimeError, "控制状态改变"):
                MockSession(1).execute("bridge", "stop")
        self.assertEqual(request.call_count, 1)


if __name__ == "__main__":
    unittest.main()
