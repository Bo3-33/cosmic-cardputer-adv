// SPDX-License-Identifier: GPL-3.0-or-later
// Run by pair-authenticator.py inside the pinned reference CLI checkout.
import { createCipheriv, pbkdf2Sync, randomBytes } from "node:crypto";
import { mkdirSync, existsSync, openSync, writeFileSync, closeSync, fsyncSync, renameSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { createInterface } from "node:readline/promises";
import { loginWithPassword, ensureFullScope, sessionNeedsTotpUpgrade, normalizeUsername } from "./src/proton/auth.ts";
import { fetchUser, fetchKeySalts, decryptUserKeys, decryptAuthenticatorKeyBlob } from "./src/proton/users.ts";
import { listAuthenticatorKeys, fetchAllEntries } from "./src/proton/authenticator-api.ts";
import { decryptEntries } from "./src/wasm/service.ts";
import { APP_VERSION } from "./src/proton/constants.ts";

async function ask(label: string): Promise<string> {
    const r=createInterface({input:process.stdin,output:process.stderr});
    try{return (await r.question(label)).trim();}finally{r.close();}
}
async function hidden(label:string):Promise<string>{
    if(!process.stdin.isTTY)throw new Error("Run pairing in an interactive terminal.");
    process.stderr.write(label);process.stdin.setRawMode(true);process.stdin.resume();
    return new Promise((resolve,reject)=>{
        let value="";
        function finish(){process.stdin.off("data",read);process.stdin.setRawMode(false);process.stdin.pause();process.stderr.write("\n");}
        function read(chunk:Buffer){for(const c of chunk.toString("utf8")){
            if(c==="\u0003"){finish();reject(new Error("Cancelled"));return;}
            if(c==="\r"||c==="\n"){finish();resolve(value);return;}
            if(c==="\u007f"||c==="\b"){if(value.length){value=value.slice(0,-1);process.stderr.write("\b \b");}}
            else if(c>=" "&&c!=="\u007f"){value+=c;process.stderr.write("*");}
        }}process.stdin.on("data",read);
    });
}
export function encryptVault(model:unknown,passphrase:string):Buffer{
    const plain=Buffer.from(JSON.stringify(model));
    if(plain.length>49152)throw new Error("Vault exceeds the Cardputer's 48 KiB limit.");
    const salt=randomBytes(16),nonce=randomBytes(12),key=pbkdf2Sync(passphrase,salt,120000,32,"sha256");
    try{
        const c=createCipheriv("aes-256-gcm",key,nonce);c.setAAD(Buffer.from("cosmic.authenticator.v1"));
        const ciphertext=Buffer.concat([c.update(plain),c.final()]);
        const h=Buffer.alloc(40);h.write("CSAUTH01",0,"ascii");h.writeUInt32LE(120000,8);salt.copy(h,12);nonce.copy(h,28);
        return Buffer.concat([h,ciphertext,c.getAuthTag()]);
    }finally{key.fill(0);plain.fill(0);}
}
async function main(){
    const destination=resolve(process.argv[2]??"");
    if(!process.argv[2])throw new Error("An output vault filename is required.");
    if(existsSync(destination))throw new Error("Vault already exists. Choose a new file and keep your existing vault as backup.");
    console.error("One-time Proton pairing. No password or decrypted vault is written to disk.");
    console.error("Enable sync in the official Proton Authenticator app first. This tool only reads cloud entries.");
    const username=await ask("Proton username or email: ");let password=await hidden("Proton password: ");
    const totp=await ask("Proton login 2FA code (blank if disabled): ");
    let session=await loginWithPassword({username:normalizeUsername(username),password,totp:totp||undefined});
    if(sessionNeedsTotpUpgrade(session))session=await ensureFullScope(session,totp||await ask("Current login 2FA code: "));
    const user=await fetchUser(session),salts=await fetchKeySalts(session);
    const userKeys=await decryptUserKeys(user,password,salts);password="";
    const remoteKeys=await listAuthenticatorKeys(session);
    if(remoteKeys.length>8)throw new Error("More than eight authenticator keys are unsupported.");
    const keys:Array<{id:string,key:string}>=[];
    for(const remote of remoteKeys){
        const userKey=userKeys.find(k=>k.ID===remote.UserKeyID);if(!userKey)continue;
        const bytes=await decryptAuthenticatorKeyBlob(remote.Key,userKey);
        if(bytes.length!==32)throw new Error("Unexpected authenticator key length.");
        keys.push({id:remote.KeyID,key:Buffer.from(bytes).toString("base64")});bytes.fill(0);
    }
    if(!keys.length)throw new Error("No usable authenticator keys. Enable official Proton sync first.");
    const remote=await fetchAllEntries(session);
    if(remote.length>64)throw new Error("The Cardputer currently supports at most 64 accounts.");
    const entries=[];
    for(const e of remote){
        const key=keys.find(k=>k.id===e.AuthenticatorKeyID);
        if(!key||e.ContentFormatVersion!==1)throw new Error("Unknown key or content version. Pairing stopped without creating a vault.");
        const bytes=Buffer.from(key.key,"base64");
        try{const models=await decryptEntries([Buffer.from(e.Content,"base64")],bytes);if(models.length!==1)throw new Error("Entry validation failed.");}
        finally{bytes.fill(0);}
        entries.push({Content:e.Content,ContentFormatVersion:e.ContentFormatVersion,AuthenticatorKeyID:e.AuthenticatorKeyID});
    }
    let passphrase=await hidden("New Cardputer vault passphrase (12–64 ASCII characters): ");
    if(passphrase.length<12||passphrase.length>64||!/^[\x20-\x7e]+$/.test(passphrase))throw new Error("Use 12–64 printable ASCII characters for the vault passphrase.");
    let confirmation=await hidden("Repeat vault passphrase: ");if(passphrase!==confirmation)throw new Error("Passphrases did not match.");confirmation="";
    const model={format:"cosmic.authenticator",version:1,appVersion:APP_VERSION,session:{UID:session.UID,AccessToken:session.AccessToken,RefreshToken:session.RefreshToken},keys,entries,lastSync:Math.floor(Date.now()/1000)};
    const encrypted=encryptVault(model,passphrase);passphrase="";
    mkdirSync(dirname(destination),{recursive:true,mode:0o700});
    const temp=destination+".new";const fd=openSync(temp,"wx",0o600);
    try{writeFileSync(fd,encrypted);fsyncSync(fd);}finally{closeSync(fd);}
    renameSync(temp,destination);
    console.error(`Saved encrypted vault with ${entries.length} accounts to ${destination}`);
    console.error("Open Apps > Authenticator, unlock, then set its clock. Wi-Fi sync now runs directly on the Cardputer.");
}
if(import.meta.main)main().catch(()=>{console.error("Pairing failed. Check credentials, supported login options, CAPTCHA requirements, network, and available SD space. No secrets were printed.");process.exitCode=1;});
