# c42-adbtool

Unofficial tool for editing the contents of CrashPlan's adb/udb databases.

n.b. it'd be easy to accidentally trash your backup by doing this.

## About the ADB directory

First you'll need to stop the CrashPlan service so that it releases its lock on the database:

Windows - `net stop "Code42 Service"` (or if you installed it for a single user, End Task the Code42 process)
macOS - `sudo launchctl unload /Library/LaunchDaemons/com.code42.service.plist`   
Linux - `sudo /usr/local/crashplan/bin/service.sh stop`  
Other - https://support.code42.com/Incydr/Agent/Troubleshooting/Stop_and_start_the_Code42_app_service

The ADB directory is found here: (and the UDB directory is next to it)

Windows - `C:\ProgramData\CrashPlan\conf\adb` or `C:\Users\<username>\AppData\<Local or Roaming>\CrashPlan\conf\adb`  
macOS - `/Library/Application Support/CrashPlan/conf/adb` or `~/Library/Application Support/CrashPlan/conf/adb`  
Linux - `/usr/local/crashplan/conf/adb`  

The adb directory should contain a list of files similar to this:

```
000630.ldb
000632.ldb
000637.log
CURRENT
LOCK
LOG
MANIFEST-000636
```

On Windows, the adb directory is owned by SYSTEM, so you'll need to take ownership of it to access it.
On Linux and macOS, you'll need to run c42-adbtool as root (e.g. using sudo).

## Disclaimer

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## ADB database encryption

The ADB/UDB database is encrypted with a computer-specific obfuscation key, so if you're not editing it on the same 
computer that produced it you'll need to supply the serial number needed to derive that key.

**On Windows**, CrashPlan uses DPAPI, so you *must* edit adb on the same computer that produced it. 

If you've installed CrashPlan for a single user, you don't need to do anything special, c42-adbtool will pick up your
encryption key automatically.

However, if you installed it for "all users", the adb directory is encrypted by the "Local System" account, so we need 
to use a tool called "PsExec" to run c42-adbtool as Local System. Download it from here and put PsExec.exe into the same 
directory as c42-adbtool:

https://learn.microsoft.com/en-us/sysinternals/downloads/psexec

Now press the start button and type "cmd". Right click on "command prompt" and click "run as administrator". Change into
the c42-adbtool directory, and now you can run adbtool like so: 

    psexec.exe -c -s c42-adbtool.exe list-keys --adb

Since c42-adbtool is executed "remotely" by psexec, you can't supply new values from stdin, so to write a large key you 
can use --value-file instead and supply an absolute filesystem path:

    psexec.exe -c -s c42-adbtool.exe write --adb --key Example --value-file "C:\Users\Joe Bloggs\example.txt"

**On macOS**, the adb directory is encrypted using your Mac's serial number. c42-adbtool will read this for you 
automatically if you run it on the same Mac that the adb directory was created on.

If you're running c42-adbtool on a different machine, you can look up your device's serial number in 
[your Apple ID settings](https://appleid.apple.com/account/manage), just click the Devices menu and select your device
to see its serial number. Note that this is case-sensitive. Pass it to c42-adbtool like so:

    ./c42-adbtool --mac-serial C02TM2ZBHX87 ...

**On Linux**, the adb directory is encrypted using a key derived from the machine-id, like so:

    cat /var/lib/dbus/machine-id /etc/machine-id 2> /dev/null

c42-adbtool will read this for you automatically if you run it on the same machine that the adb directory was created on.
Otherwise, you can run that command on the original machine and then supply that value to c42-adbtool like so:

    ./c42-adbtool --linux-serial "c3fdd72a687e256f93a8dc04636dd8ac
    c3fdd72a687e256f93a8dc04636dd8ac
    " ...

Note that in this case both of my machine-id files ended with a newline character, so I have to include those when 
supplying the value to c42-adbtool (so the closing quote ends up on a line on its own).

## Usage

```
Read and modify Code42/CrashPlan UDB and ADB databases

Usage: c42-adbtool <command> [--options]

Options:
  --help                 shows this page
  --adb                  operate on the adb database (default)
  --udb                  operate on the udb database
  --path arg             path to CrashPlan's 'adb' or 'udb' directory to
                         operate on (omit to locate automatically)
  --mac-serial arg       serial number of the Mac that matches the adb
                         directory (for CrashPlan Small Business, optional)
  --linux-serial arg     serial number of the Linux machine that matches the
                         adb directory (for CrashPlan Small Business, optional)

Read/write command options:
  --key arg              key to read/write from (required)
  --value arg            value to write (optional, omit to read from stdin)
  --value-file arg       file to read/write value from instead of supplying
                         directly (optional)
  --format arg (=raw)    encoding for read/write values ('raw', 'hex')

Commands:
  read      - Read the value of a key
  write     - Write a value to a key
  delete    - Delete a key
  list      - List all keys and values in the database
  list-keys - List all keys in the database
```

Use the `list` or `list-keys` commands to see what fields you have in your database:

```bash
$ sudo ./c42-adbtool list-keys --adb
ACCESSIBLE_KEY
ArchiveDataKey
ArchiveSecureDataKey
accessToken
accessTokenExpiration
adb-initialized
authRules
authority_fallback_list_cleared
compliance_enforce
mostRecentFullDiskAccessResult

$ sudo ./c42-adbtool list-keys --udb
ACCESSIBLE_KEY
SERVICE_CONFIG
SERVICE_MODEL
cps_pid_list
duplicateGuidDetection_counter
offlinePasswordHash
savedCloakingConfiguration
transportPrivateKey
transportPublicKey
udb-initialized
ui_http_keystore
ui_http_keystorePassword
```

To read the value of a specific key, use the `read` command:

```bash
$ sudo ./c42-adbtool read --adb --key compliance_enforce --format hex
00
```

Or read the value into a file instead of printing it to stdout:

```bash
$ sudo ./c42-adbtool read --adb --key compliance_enforce --value-file output
$ xxd output
00
```

To write a value to a key, use the `write` command:

```bash
$ sudo ./c42-adbtool write --adb --key compliance_enforce --format hex --value 01
```

Or you can supply a value from stdin, which makes editing larger values easy:

```
$ sudo ./c42-adbtool read --udb --key SERVICE_CONFIG > my.service.xml

# Edit it here, and ...

$ sudo ./c42-adbtool write --udb --key SERVICE_CONFIG < my.service.xml
```

Or from a file:

```
$ sudo ./c42-adbtool write --udb --key SERVICE_CONFIG --value-file my.service.xml
```

## Building c42-adbtool

If you don't want to use one of the precompiled releases from the Releases tab above, you can build c42-adbtool yourself. 
You need a C++ compiler, make and cmake installed (e.g. `apt install build-essential make cmake git` on Ubuntu Xenial).
Clone this repository, then run `make`, and all of the libraries will be fetched and built, followed by c42-adbtool itself.

On Windows, build c42-adbtool using [Msys2](https://www.msys2.org/)'s UCRT64 environment, install these packages,
and build with "make":

```
$ pacman -S git make mingw-w64-ucrt-x86_64-{make,cmake,ninja,gcc}
$ make
```
