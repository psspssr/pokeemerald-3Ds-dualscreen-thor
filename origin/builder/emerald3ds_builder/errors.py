"""Errors the builder reports to the player in plain language."""


class BuilderError(Exception):
    """A problem the player can act on. `str(error)` is the message to show."""

    def __init__(self, message: str, hint: str = ""):
        super().__init__(message)
        self.message = message
        self.hint = hint

    def __str__(self) -> str:
        return self.message + ("\n\n" + self.hint if self.hint else "")
