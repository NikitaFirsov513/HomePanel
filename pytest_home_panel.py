"""Hardware smoke test; requires pytest-embedded-idf and an attached ESP32-P4."""
import logging
from pathlib import Path

import pytest
from pytest_embedded_idf.dut import IdfDut


@pytest.mark.esp32p4
def test_home_panel(dut: IdfDut) -> None:
    binary = Path(dut.app.binary_path) / "home_panel.bin"
    assert binary.is_file()
    assert 0 < binary.stat().st_size <= 0x400000
    logging.info("home_panel.bin size: %d bytes", binary.stat().st_size)
    dut.expect_exact("Application starting", timeout=30)
    dut.expect_exact("UI initialized", timeout=60)
