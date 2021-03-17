from typing import Any
from typing import List
from typing import Union


class MsgpackEncoder(object):
    content_type: str
    def _decode(self, data: Union[str, bytes]) -> Any: ...
    def encode_trace(self, trace: List[Any]) -> bytes: ...
    def join_encoded(self, objs: List[bytes]) -> bytes: ...
