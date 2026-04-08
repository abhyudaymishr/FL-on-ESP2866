from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from .generator import SlideSpec

__all__ = ["SlideSpec", "default_slides", "generate_project_deck"]


def __getattr__(name: str) -> Any:
    if name == "SlideSpec":
        from .generator import SlideSpec

        return SlideSpec
    if name == "default_slides":
        from .generator import default_slides

        return default_slides
    if name == "generate_project_deck":
        from .generator import generate_project_deck

        return generate_project_deck
    raise AttributeError(name)
