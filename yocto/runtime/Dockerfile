FROM scratch

ADD rootfs.tar.gz /
COPY entrypoint.sh /usr/bin/dirtsim-entrypoint
RUN chmod 0755 /usr/bin/dirtsim-entrypoint

ENTRYPOINT ["/usr/bin/dirtsim-entrypoint"]
CMD ["/bin/sh"]
