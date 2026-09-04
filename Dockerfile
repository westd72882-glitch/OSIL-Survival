# ==================== ОБРАЗ ВЫДЕЛЕННОГО СЕРВЕРА ====================
# Собирается ровно тот же osil_server, что и локально: ни SDL, ни OpenGL ему не нужны,
# поэтому образ получается маленьким и запускается на бесплатном тарифе render.com.
#
# Хостинг сам назначает порт через переменную PORT — сервер её читает (см. ServerApp).
# Проверка живости идёт на «/», этот адрес отдаёт карточку сервера.
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ cmake make ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src
COPY tests ./tests
COPY config ./config
# Собираем только сервер: клиент требует SDL2, а он в образе не нужен.
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target osil_server -j"$(nproc)"

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /src/build/bin/osil_server /app/osil_server
COPY --from=build /src/config /app/config
ENV PORT=28015
EXPOSE 28015
CMD ["/app/osil_server"]
