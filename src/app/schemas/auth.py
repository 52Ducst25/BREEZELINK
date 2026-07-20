"""Auth DTOs: register / login / refresh / token pair / me / password reset."""

import uuid

from pydantic import BaseModel, EmailStr, Field


class RegisterRequest(BaseModel):
    """Redeem an activation code into an account.

    Registration no longer names an organization: the vendor already created
    it when recording the sale, and ``code`` is what points at it. Letting the
    caller pass ``org_name`` again would re-open self-service org creation —
    the exact thing the code flow exists to close.
    """

    code: str = Field(min_length=4, max_length=16)
    email: EmailStr
    password: str = Field(min_length=8, max_length=128)
    full_name: str | None = Field(default=None, max_length=200)
    phone: str | None = Field(default=None, max_length=20)


class LoginRequest(BaseModel):
    email: EmailStr
    password: str = Field(min_length=1, max_length=128)


class RefreshRequest(BaseModel):
    refresh_token: str


class TokenPair(BaseModel):
    access_token: str
    refresh_token: str
    token_type: str = "bearer"


class AccessToken(BaseModel):
    access_token: str
    token_type: str = "bearer"


class MeResponse(BaseModel):
    """``GET /auth/me`` — the caller's own identity (Flutter user_profile)."""

    id: uuid.UUID
    email: EmailStr
    org_id: uuid.UUID
    role: str
    full_name: str | None = None
    phone: str | None = None
    # Household address — shown on the app's location card (BenKon-style).
    location: str | None = None
    latitude: float | None = None
    longitude: float | None = None


class UpdateProfileRequest(BaseModel):
    """``PUT /auth/me`` — the caller edits their own profile. All optional; only
    the fields sent are changed (so the app can save just the address, or the
    map picker just the coordinates + reverse-geocoded address)."""

    full_name: str | None = Field(default=None, max_length=200)
    phone: str | None = Field(default=None, max_length=20)
    location: str | None = Field(default=None, max_length=200)
    latitude: float | None = Field(default=None, ge=-90, le=90)
    longitude: float | None = Field(default=None, ge=-180, le=180)


class ForgotPasswordRequest(BaseModel):
    email: EmailStr


class ResetPasswordRequest(BaseModel):
    token: str
    new_password: str = Field(min_length=8, max_length=128)
