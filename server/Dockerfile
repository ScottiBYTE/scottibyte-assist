FROM node:22-alpine

WORKDIR /app

COPY package.json ./
RUN npm install --omit=dev

COPY src ./src
COPY public ./public
COPY release.json ./release.json

ENV NODE_ENV=production
ENV HOST=0.0.0.0
ENV PORT=3089

EXPOSE 3089

CMD ["npm", "start"]
