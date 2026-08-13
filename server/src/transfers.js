import {
  createHash,
  randomUUID
} from 'node:crypto';

import {
  createReadStream,
  createWriteStream
} from 'node:fs';

import {
  mkdir,
  rm,
  stat
} from 'node:fs/promises';

import path from 'node:path';

const transferRoot =
  process.env.ASSIST_TRANSFER_DIR ??
  '/tmp/scottibyte-assist-transfers';

const transfers = new Map();

const maximumTransferBytes =
  1024 * 1024 * 1024;

function safeFileName(value) {
  if (typeof value !== 'string') {
    return '';
  }

  const cleaned =
    value
      .replaceAll('\\', '/')
      .split('/')
      .pop()
      ?.trim() ?? '';

  if (
    !cleaned ||
    cleaned === '.' ||
    cleaned === '..'
  ) {
    return '';
  }

  return cleaned.slice(0, 255);
}

function publicTransfer(transfer) {
  return {
    id: transfer.id,
    sessionCode: transfer.sessionCode,
    senderRole: transfer.senderRole,
    recipientRole: transfer.recipientRole,
    fileName: transfer.fileName,
    declaredSize: transfer.declaredSize,
    storedSize: transfer.storedSize,
    sha256: transfer.sha256,
    status: transfer.status,
    createdAt: transfer.createdAt
  };
}

export async function createTransfer({
  sessionCode,
  senderRole,
  fileName,
  declaredSize
}) {
  const normalizedName =
    safeFileName(fileName);

  if (!normalizedName) {
    return {
      error: 'invalid_file_name',
      message:
        'A valid file name is required.'
    };
  }

  if (
    !Number.isSafeInteger(declaredSize) ||
    declaredSize < 0 ||
    declaredSize > maximumTransferBytes
  ) {
    return {
      error: 'invalid_file_size',
      message:
        'The file size is not valid.'
    };
  }

  const id = randomUUID();

  const recipientRole =
    senderRole === 'customer'
      ? 'supporter'
      : 'customer';

  await mkdir(
    transferRoot,
    {
      recursive: true,
      mode: 0o700
    }
  );

  const transfer = {
    id,
    sessionCode,
    senderRole,
    recipientRole,
    fileName: normalizedName,
    declaredSize,
    storedSize: null,
    sha256: null,
    status: 'WAITING_UPLOAD',
    createdAt: new Date().toISOString(),
    path: path.join(
      transferRoot,
      `${id}.part`
    )
  };

  transfers.set(id, transfer);

  return {
    transfer: publicTransfer(transfer)
  };
}

export function getTransfer(
  sessionCode,
  transferId
) {
  const transfer =
    transfers.get(transferId);

  if (
    !transfer ||
    transfer.sessionCode !== sessionCode
  ) {
    return null;
  }

  return transfer;
}

export function transferView(transfer) {
  return publicTransfer(transfer);
}

export async function receiveTransferBody(
  transfer,
  request
) {
  if (transfer.status !== 'WAITING_UPLOAD') {
    return {
      error: 'invalid_transfer_state',
      message:
        'The transfer is not waiting for an upload.'
    };
  }

  transfer.status = 'UPLOADING';

  const hash =
    createHash('sha256');

  let bytes = 0;

  try {
    await new Promise(
      (resolve, reject) => {
        const output =
          createWriteStream(
            transfer.path,
            {
              flags: 'wx',
              mode: 0o600
            }
          );

        const fail = (error) => {
          request.unpipe(output);
          output.destroy();
          reject(error);
        };

        request.on(
          'data',
          (chunk) => {
            bytes += chunk.length;

            if (
              bytes >
              maximumTransferBytes
            ) {
              fail(
                new Error(
                  'transfer_too_large'
                )
              );
              return;
            }

            hash.update(chunk);
          }
        );

        request.on('error', fail);
        output.on('error', reject);
        output.on('finish', resolve);

        request.pipe(output);
      }
    );

    if (bytes !== transfer.declaredSize) {
      await rm(
        transfer.path,
        {
          force: true
        }
      );

      transfer.status =
        'WAITING_UPLOAD';

      return {
        error: 'size_mismatch',
        message:
          'Uploaded byte count does not match the declared file size.',
        expected: transfer.declaredSize,
        received: bytes
      };
    }

    const info =
      await stat(transfer.path);

    transfer.storedSize =
      info.size;

    transfer.sha256 =
      hash.digest('hex');

    transfer.status = 'READY';

    return {
      transfer:
        publicTransfer(transfer)
    };
  } catch (error) {
    await rm(
      transfer.path,
      {
        force: true
      }
    );

    transfer.status =
      'WAITING_UPLOAD';

    return {
      error: 'upload_failed',
      message:
        error.message ===
        'transfer_too_large'
          ? 'The uploaded file exceeds the transfer limit.'
          : 'The file upload failed.'
    };
  }
}

export function createTransferReadStream(
  transfer
) {
  if (transfer.status !== 'READY') {
    return null;
  }

  return createReadStream(
    transfer.path
  );
}

export async function deleteTransfer(
  transfer
) {
  transfers.delete(
    transfer.id
  );

  await rm(
    transfer.path,
    {
      force: true
    }
  );
}
