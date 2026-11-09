package token

import (
	"AdaptixServer/core/profile"
	"crypto/rand"
	"encoding/base64"
	"errors"
	"net/http"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/golang-jwt/jwt/v5"
)

var accessKey string
var refreshKey string
var accessTokenLiveHours int
var refreshTokenLiveHours int

type Claims struct {
	Username string `json:"username"`
	Version  string `json:"version"`
	jwt.RegisteredClaims
}

func InitJWT(accessTokenHours int, refreshTokenHours int) {
	accessKey, _ = generateRandomKey(32)
	refreshKey, _ = generateRandomKey(32)
	accessTokenLiveHours = accessTokenHours
	refreshTokenLiveHours = refreshTokenHours
}

func generateRandomKey(length int) (string, error) {
	bytes := make([]byte, length)
	if _, err := rand.Read(bytes); err != nil {
		return "", err
	}
	return base64.URLEncoding.EncodeToString(bytes), nil
}

func GenerateAccessToken(username string, version string) (string, error) {
	expirationTime := time.Now().Add(time.Duration(accessTokenLiveHours) * time.Hour)
	claims := &Claims{
		Username: username,
		Version:  version,
		RegisteredClaims: jwt.RegisteredClaims{
			ExpiresAt: jwt.NewNumericDate(expirationTime),
		},
	}

	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	tokenString, err := token.SignedString([]byte(accessKey))
	if err != nil {
		return "", err
	}
	return tokenString, nil
}

func GenerateRefreshToken(username string, version string) (string, error) {
	expirationTime := time.Now().Add(time.Duration(refreshTokenLiveHours) * time.Hour)
	claims := &Claims{
		Username: username,
		Version:  version,
		RegisteredClaims: jwt.RegisteredClaims{
			ExpiresAt: jwt.NewNumericDate(expirationTime),
		},
	}

	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	tokenString, err := token.SignedString([]byte(refreshKey))
	if err != nil {
		return "", err
	}
	return tokenString, nil
}

func ValidateAccessToken() gin.HandlerFunc {
	return func(ctx *gin.Context) {
		tokenString := ctx.GetHeader("Authorization")
		if tokenString == "" {
			// ★ 2026-11-09 MCP：部分 MCP 客户端只能把凭证拼进 URL —— 允许
			//   ?access_token= 兜底（与 header 同一 JWT，权限完全相同）。
			tokenString = ctx.Query("access_token")
			if tokenString != "" {
				tokenString = "Bearer " + tokenString
			}
		}
		if tokenString == "" {
			_ = ctx.Error(errors.New("authorization token required"))
			return
		}

		if strings.HasPrefix(tokenString, "Bearer ") {
			tokenString = tokenString[7:]
		} else {
			_ = ctx.Error(errors.New("authorization token required"))
			return
		}

		// ★ 静态 API token（profile 的 Teamserver.api_token）：constant-time
		//   比对，通过即与 JWT 同权限 —— MCP/自动化客户端用它免 login、跨重启。
		if profile.CheckStaticApiToken(tokenString) {
			ctx.Set("username", "api-token")
			ctx.Set("version", "")
			ctx.Next()
			return
		}

		claims := &Claims{}
		jwtToken, err := jwt.ParseWithClaims(tokenString, claims, func(token *jwt.Token) (interface{}, error) {
			return []byte(accessKey), nil
		})
		if err != nil || !jwtToken.Valid {
			_ = ctx.Error(errors.New("invalid token"))
			return
		}

		ctx.Set("username", claims.Username)
		ctx.Set("version", claims.Version)
		ctx.Next()
	}
}

func RefreshTokenHandler(ctx *gin.Context) {

	refreshToken := ctx.GetHeader("Authorization")
	if refreshToken == "" {
		ctx.AbortWithStatusJSON(http.StatusUnauthorized, gin.H{"error": "unauthorized", "message": "refresh token required"})
		return
	}

	if strings.HasPrefix(refreshToken, "Bearer ") {
		refreshToken = refreshToken[7:]
	} else {
		ctx.AbortWithStatusJSON(http.StatusUnauthorized, gin.H{"error": "unauthorized", "message": "refresh token required"})
		return
	}

	claims := &Claims{}
	token, err := jwt.ParseWithClaims(refreshToken, claims, func(token *jwt.Token) (interface{}, error) {
		return []byte(refreshKey), nil
	})

	if err != nil || !token.Valid {
		ctx.AbortWithStatusJSON(http.StatusUnauthorized, gin.H{"error": "unauthorized", "message": "invalid refresh token"})
		return
	}

	newAccessToken, err := GenerateAccessToken(claims.Username, claims.Version)
	if err != nil {
		ctx.AbortWithStatusJSON(http.StatusInternalServerError, gin.H{"error": "server_error", "message": "could not generate access token"})
		return
	}

	ctx.JSON(http.StatusOK, gin.H{"access_token": newAccessToken})
}
