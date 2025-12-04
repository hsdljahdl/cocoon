"""Machine translation tools."""

from mt.translate import (
    translate,
    TranslateConfig,
    TranslationResult,
    TimingInfo,
    add_translate_args,
    config_from_args,
    load_config_from_file,
)

__all__ = [
    "translate",
    "TranslateConfig",
    "TranslationResult",
    "TimingInfo",
    "add_translate_args",
    "config_from_args",
    "load_config_from_file",
]

