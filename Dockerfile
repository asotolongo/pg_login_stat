FROM postgres:17

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        postgresql-server-dev-17 \
        gcc \
        make \
        libkrb5-dev \
    && rm -rf /var/lib/apt/lists/*

# Create directory for custom pg_hba.conf
RUN mkdir -p /etc/postgresql

# Copy extension source and compile
COPY . /tmp/pg_login_stat/
RUN cd /tmp/pg_login_stat && \
    make && \
    make install && \
    rm -rf /tmp/pg_login_stat
