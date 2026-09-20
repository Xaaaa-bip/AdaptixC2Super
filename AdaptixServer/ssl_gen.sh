# ECDSA recommendation key ≥ secp384r1
# List ECDSA the supported curves (openssl ecparam -list_curves)
# openssl req -x509 -nodes -newkey ec:secp384r1 -keyout server.ecdsa.key -out server.ecdsa.crt -days 3650

# RSA recommendation key ≥ 2048-bit
openssl req -x509 -nodes -newkey rsa:2048 -keyout server.rsa.key -out server.rsa.crt -days 3650

# Plausible subject + SAN (same idea as Teamserver.tls_autogen): see scripts/gen_teamserver_disguise_tls.sh
