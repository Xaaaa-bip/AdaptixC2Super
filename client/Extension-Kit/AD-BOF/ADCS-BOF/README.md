# ADCS-BOF

A library of beacon object files to interact with ADCS servers and certificates.



## certi auth

Authenticate with certificate (PKINIT + UnPAC-the-hash)

```
certi auth <{--cert base64_cert} || {--pfx file.pfx}> [--password pfx_password] [--dc DC] [--no-unpac]
```

* `--cert` - Base64 encoded PFX certificate
* `--pfx` - PFX certificate file
* `--password` - PFX password
* `--dc` - Domain Controller address (auto-detected if not specified)
* `--no-unpac` - Only get TGT, don't extract NT hash



## certi enum

Enumerate CAs and templates in the AD

```
certi enum
```



## certi request

Request an enrollment certificate 

```
certi req <--ca CA> [--template Template] [--subject CN] [--altname CN] [--alturl tag:microsoft.com,2022-09-14:sid:<SID>] [--install] [--machine] [--policy] [--dns] [--pfx-password password] [--pem]
```

* `--ca` - The certificate authority to use
* `--template` - The certificate type to request (else default for User/Machine)
* `--subject` - The subject's distinguished name (else default for user/machine)
* `--altname` - The alternate subject's distinguished name
* `--alturl` - SAN URL entry, can be used to specify the alternate subject's SID
* `--install` - Install the certificate in current context?
* `--pfx-password` - Output PFX password
* `--machine` - Request a certificate for a machine instead of a user?
* `--policy` - Adds App policy to allow client auth and Acting as a certificate agent (for ESC15)
* `--dns` - Subject Altname given as a DNS name (else: Subject alt name given as UPN).
* `--pem` - Output in PEM format instead of PFX



## certi request_on_behalf

Request certificate on behalf of another user (ESC3)

```
certi request_on_behalf --ca <ca> --template <template> --target <target> --ea-pfx <cert> [--ea-password password] [--pfx-password password] [--pem]
```

* `--ca` - The certificate authority to use
* `--template` - The certificate type to request (else default for User/Machine)
* `--target` - Target user (DOMAIN\\username)
* `--altname` - The alternate subject's distinguished name
* `--ea-pfx` - Enrollment Agent certificate (PFX file
* `--ea-password` - Enrollment Agent PFX password
* `--pfx-password` - Output PFX password
* `--pem` - Output in PEM format instead of PFX



## certi shadow

Shadow Credentials attack - write KeyCredentialLink and get certificate

```
certi shadow --target <account> [--clear] [--domain domain] [--no-write]
```

* `--target` - Target user (sAMAccountName)
* `--clear` - Clear msDS-KeyCredentialLink (don't write new, only clear)
* `--domain` - Domain name (auto-detected if not specified)
* `--no-write` - Don't write to AD, just generate certificate



## References
- [https://github.com/trustedsec/CS-Remote-OPs-BOF](https://github.com/trustedsec/CS-Remote-OPs-BOF)
- [https://github.com/trustedsec/CS-Situational-Awareness-BOF](https://github.com/trustedsec/CS-Situational-Awareness-BOF)
- [https://github.com/RayRRT/ESC1-unPAC]
- [https://github.com/RayRRT/BOFs/tree/main/ShadowCreds-unPAC-BOF]