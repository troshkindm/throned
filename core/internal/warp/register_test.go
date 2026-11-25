package warp

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/ed25519"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"encoding/base64"
	"encoding/json"
	"encoding/pem"
	"io"
	"net/http"
	"reflect"
	"testing"
)

type testTransport func(*http.Request) (*http.Response, error)

func (f testTransport) RoundTrip(request *http.Request) (*http.Response, error) {
	return f(request)
}

func TestEnrollMASQUEUsesTLSCapableEndpoint(t *testing.T) {
	peerKey, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	peerDER, err := x509.MarshalPKIXPublicKey(&peerKey.PublicKey)
	if err != nil {
		t.Fatal(err)
	}
	var response device
	response.Config.Interface.Addresses.V4 = "198.51.100.2"
	response.Config.Interface.Addresses.V6 = "2001:db8::2"
	if err := json.Unmarshal([]byte(`{"config":{"peers":[{"endpoint":{"host":"198.51.100.1:443"}}]}}`), &response); err != nil {
		t.Fatal(err)
	}
	response.Config.Peers[0].PublicKey = string(pem.EncodeToMemory(&pem.Block{Type: "PUBLIC KEY", Bytes: peerDER}))
	var requestKey *ecdsa.PublicKey
	apiClient := &client{httpClient: &http.Client{Transport: testTransport(func(request *http.Request) (*http.Response, error) {
		if request.Method != http.MethodPatch || request.URL.Path != "/v0a4471/reg/test-device" {
			t.Fatalf("unexpected enrollment request: %s %s", request.Method, request.URL.Path)
		}
		if request.Header.Get("Authorization") != "Bearer test-token" {
			t.Fatal("missing registration token")
		}
		var body updateKeyRequest
		if err := json.NewDecoder(request.Body).Decode(&body); err != nil {
			t.Fatal(err)
		}
		if body.KeyType != "secp256r1" || body.TunnelType != TunnelMASQUE {
			t.Fatalf("unexpected key enrollment: %+v", body)
		}
		der, err := base64.StdEncoding.DecodeString(body.Key)
		if err != nil {
			t.Fatal(err)
		}
		key, err := x509.ParsePKIXPublicKey(der)
		if err != nil {
			t.Fatal(err)
		}
		requestKey = key.(*ecdsa.PublicKey)
		content, err := json.Marshal(response)
		if err != nil {
			t.Fatal(err)
		}
		return &http.Response{StatusCode: http.StatusOK, Body: io.NopCloser(bytes.NewReader(content))}, nil
	})}}
	identity := &Identity{}
	if err := enrollMASQUE(context.Background(), apiClient, defaultAPIHosts[0],
		&device{ID: "test-device", Token: "test-token"}, identity); err != nil {
		t.Fatal(err)
	}
	// The API's UDP endpoint cannot carry the HTTP/2 fallback.
	if identity.Endpoint != "162.159.198.2:443" {
		t.Fatalf("HTTP/2 endpoint = %q", identity.Endpoint)
	}
	if identity.PeerPublicKey != base64.StdEncoding.EncodeToString(peerDER) {
		t.Fatal("server public key was not preserved")
	}
	privateDER, err := base64.StdEncoding.DecodeString(identity.PrivateKey)
	if err != nil {
		t.Fatal(err)
	}
	privateKey, err := x509.ParseECPrivateKey(privateDER)
	if err != nil {
		t.Fatal(err)
	}
	if requestKey == nil || !privateKey.PublicKey.Equal(requestKey) {
		t.Fatal("stored private key does not match the enrolled public key")
	}
	if identity.IPv4 != "198.51.100.2" || identity.IPv6 != "2001:db8::2" {
		t.Fatalf("tunnel addresses changed: %s %s", identity.IPv4, identity.IPv6)
	}
}

func TestWireGuardReservedBytes(t *testing.T) {
	var response device
	if err := json.Unmarshal([]byte(`{"config":{"client_id":"AQID","interface":{"addresses":{"v4":"198.51.100.2"}},"peers":[{"public_key":"test-peer-key","endpoint":{"host":"proxy.example:2408"}}]}}`), &response); err != nil {
		t.Fatal(err)
	}
	key, err := GeneratePrivateKey()
	if err != nil {
		t.Fatal(err)
	}
	identity := &Identity{}
	if err := fillWireGuard(&response, key, identity); err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(identity.Reserved, []byte{1, 2, 3}) {
		t.Fatalf("reserved bytes = %v", identity.Reserved)
	}
	if identity.Endpoint != "proxy.example:2408" || identity.PrivateKey != key.String() {
		t.Fatal("WireGuard registration changed the endpoint or key")
	}
	response.Config.ClientID = "invalid base64!"
	if err := fillWireGuard(&response, key, &Identity{}); err == nil {
		t.Fatal("accepted invalid reserved bytes")
	}
}

func TestMASQUERejectsNonECDSAPeer(t *testing.T) {
	publicKey, _, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	der, err := x509.MarshalPKIXPublicKey(publicKey)
	if err != nil {
		t.Fatal(err)
	}
	for _, invalid := range []string{"not PEM", string(pem.EncodeToMemory(&pem.Block{Type: "PUBLIC KEY", Bytes: der}))} {
		if _, err := parsePeerPublicKey(invalid); err == nil {
			t.Fatal("accepted a malformed or non-ECDSA peer key")
		}
	}
}
