"""Source budgets and native/Python registration must agree before release."""

from tests.compile_check.check_sources import inspect_sources


def test_source_contracts_and_budgets():
    report = inspect_sources()
    assert not report["errors"], "\n".join(report["errors"])
