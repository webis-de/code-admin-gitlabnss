
# For Users
1. Install from the debian package: `sudo dpkg -i nssgitlab.deb`
2. Configure NSS by adding `gitlab` to the lines in `/etc/nsswitch.conf` starting with `passwd:` and `groups:`.
3. Optionally to support SSH login (via public keys added to GitLab):
    Add these lines to `/etc/ssh/sshd_config`
    ```
    AuthorizedKeysCommand /bin/fetchgitlabkeys
    AuthorizedKeysCommandUser root
    ```
4. Modify `/etc/gitlabnss/gitlabnss.conf` to fit your needs. Particularly: set `base_url` to the GitLab API endpoint of your choice and `secret` to a **FILE** that contains the API key (requires `read_api`). Make sure that the `secret` file can only be read by root (owner: `root` and permissions `0400`). 
5. Start the service: On Ubuntu run `systemctl enable gitlabnssd` or `/etc/init.d/gitlabnssd start` to start the service.

## How it Works
**gitlabnssd** Daemon process that listens to a UNIX file socket (configured in `gitlabnss.conf`; default: `/var/run/gitlabnss.sock`) and provides means of fetching GitLab user information by ID or name. Technically, the consumers of this API (NSS and fetchgitlabkeys) could access the GitLab API directly but the API key then has to be readable by artbitrary users which is a security risk.

**NSS** `TODO`

**fetchgitlabkeys** If you want GitLab users to be able to login using SSH and the public keys configured in GitLab, you can direct the `AuthorizedKeysCommand` to use `fetchgitlabkeys` to load these keys. For reasons explained above, `fetchgitlabkeys` does not access the GitLab API directly but communicates with the daemon using `gitlabnss.sock`.


\dot
digraph G {
    subgraph Files {
        #label = "Files";
        cluster=True;
        
        secret [color=blue];
        "gitlabnss.conf" [color=blue];
    };
    subgraph Programs {
        #label = "Programs";
        cluster=True;
        
        gitlabnssd;
        authorizedkeys;
        "libnss_gitlab.so";
    };
    "gitlabnss.sock" [color=purple];
    
    {gitlabnssd, authorizedkeys, "libnss_gitlab.so"} -> "gitlabnss.conf" [color=blue];
    gitlabnssd -> secret [color=blue];
    gitlabnssd -> "gitlabnss.sock" [color=purple, label=listen];
    {authorizedkeys, "libnss_gitlab.so"} -> "gitlabnss.sock" [color=purple, label=connect];
}
\enddot


# For Developers
## Installation
1. Move `libnss_gitlab.so` into `/usr/lib/`
2. Add `gitlab` to `/etc/nsswitch.conf`

## With SSH
1. Add these lines to `/etc/ssh/sshd_config`:
```
AuthorizedKeysCommand /bin/fetchgitlabkeys
AuthorizedKeysCommandUser root
```

## Naming
https://sourceware.org/glibc/manual/latest/html_mono/libc.html#NSS-Module-Names

`_nss_<service>_<function>`


## Releasing a New Version
1. Create a new Release via the [https://github.com/webis-de/code-admin-gitlabnss/releases](GitHub Release page); A new action should start automatically to build the latest release and automatically adds the debian package to the released assets.