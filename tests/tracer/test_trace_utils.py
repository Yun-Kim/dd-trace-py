import pytest

from ddtrace import Pin, Tracer
from ddtrace.settings import Config
from ddtrace.ext import http
from ddtrace.compat import stringify
from ddtrace.contrib import trace_utils

from tests import override_global_config
from tests.tracer.test_tracer import get_dummy_tracer


@pytest.fixture
def config():
    c = Config()
    c._add("myint", dict())
    return c


@pytest.fixture
def tracer():
    tracer = get_dummy_tracer()
    return tracer


@pytest.fixture
def span(tracer):
    span = tracer.trace(name="myint")
    yield span
    span.finish()


@pytest.mark.parametrize(
    "pin,config_val,default,global_service,expected",
    [
        (Pin(), None, None, None, None),
        (Pin(), None, None, "global-svc", "global-svc"),
        (Pin(), None, "default-svc", None, "default-svc"),
        # Global service should have higher priority than the integration default.
        (Pin(), None, "default-svc", "global-svc", "global-svc"),
        (Pin(), "config-svc", "default-svc", None, "config-svc"),
        (Pin(service="pin-svc"), None, "default-svc", None, "pin-svc"),
        (Pin(service="pin-svc"), "config-svc", "default-svc", None, "pin-svc"),
    ],
)
def test_int_service(config, pin, config_val, default, global_service, expected):
    if config_val:
        config.myint.service = config_val

    if global_service:
        config.service = global_service

    assert trace_utils.int_service(pin, config.myint, default) == expected


def test_int_service_integration(config):
    pin = Pin()
    tracer = Tracer()
    assert trace_utils.int_service(pin, config.myint) is None

    with override_global_config(dict(service="global-svc")):
        assert trace_utils.int_service(pin, config.myint) is None

        with tracer.trace("something", service=trace_utils.int_service(pin, config.myint)) as s:
            assert s.service == "global-svc"


@pytest.mark.parametrize(
    "pin,config_val,default,expected",
    [
        (Pin(), None, "default-svc", "default-svc"),
        (Pin(), "config-svc", "default-svc", "config-svc"),
        (Pin(service="pin-svc"), None, "default-svc", "pin-svc"),
        (Pin(service="pin-svc"), "config-svc", "default-svc", "pin-svc"),
    ],
)
def test_ext_service(config, pin, config_val, default, expected):
    if config_val:
        config.myint.service = config_val

    assert trace_utils.ext_service(pin, config.myint, default) == expected


@pytest.mark.parametrize(
    "method,url,status_code",
    [
        ("GET", "http://localhost/", 0),
        ("GET", "http://localhost/", 200),
        (None, None, None),
    ],
)
def test_set_http_meta(span, config, method, url, status_code):
    trace_utils.set_http_meta(span, config, method=method, url=url, status_code=status_code)
    if method is not None:
        assert span.meta[http.METHOD] == method
    else:
        assert http.METHOD not in span.meta

    if url is not None:
        assert span.meta[http.URL] == stringify(url)
    else:
        assert http.URL not in span.meta

    if status_code is not None:
        assert span.meta[http.STATUS_CODE] == str(status_code)
        if 500 <= int(status_code) < 600:
            assert span.error == 1
        else:
            assert span.error == 0
    else:
        assert http.STATUS_CODE not in span.meta


@pytest.mark.parametrize(
    "error_codes,status_code,error",
    [
        ("404-400", 400, 1),
        ("400-404", 400, 1),
        ("400-404", 500, 0),
        ("500-520", 530, 0),
        ("500-550         ", 530, 1),
        ("400-404,419", 419, 1),
        ("400,401,403", 401, 1),
    ],
)
def test_set_http_meta_custom_errors(span, int_config, error_codes, status_code, error):
    config.http_server.error_statuses = error_codes
    trace_utils.set_http_meta(span, int_config, status_code=status_code)
    assert span.error == error
