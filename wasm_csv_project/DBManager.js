class DBManager {
  constructor(dbName = 'wasm_csv_db', storeName = 'files') {
    this.dbName = dbName;
    this.storeName = storeName;
    this.db = null;
  }

  async openDb() {
    if (this.db) {
      return this;
    }
    return new Promise((resolve, reject) => {
      const req = indexedDB.open(this.dbName, 1);
      req.onupgradeneeded = (e) => {
        const db = e.target.result;
        if (!db.objectStoreNames.contains(this.storeName)) {
          db.createObjectStore(this.storeName);
        }
      };
      req.onsuccess = (e) => {
        this.db = e.target.result;
        resolve(this);
      };
      req.onerror = (e) => {
        console.error(`Error opening db: ${e.target.error}`);
        reject(e.target.error);
      };
    });
  }

  async putFile(key, file) {
    await this.openDb();
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction(this.storeName, 'readwrite');
      const store = tx.objectStore(this.storeName);
      const req = store.put(file, key);
      req.onsuccess = () => resolve();
      req.onerror = (e) => {
        console.error(`Error putting file with key ${key}: ${e.target.error}`);
        reject(e.target.error);
      }
    });
  }

  async getFile(key) {
    await this.openDb();
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction(this.storeName, 'readonly');
      const store = tx.objectStore(this.storeName);
      const req = store.get(key);
      req.onsuccess = () => resolve(req.result);
      req.onerror = (e) => {
        console.error(`Error getting file with key ${key}: ${e.target.error}`);
        reject(e.target.error);
      }
    });
  }

  async deleteFile(key) {
    await this.openDb();
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction(this.storeName, 'readwrite');
      const store = tx.objectStore(this.storeName);
      const req = store.delete(key);
      req.onsuccess = () => resolve();
      req.onerror = (e) => {
        console.error(`Error deleting file with key ${key}: ${e.target.error}`);
        reject(e.target.error);
      }
    });
  }

  async clear() {
    await this.openDb();
    return new Promise((resolve, reject) => {
      const tx = this.db.transaction(this.storeName, 'readwrite');
      const store = tx.objectStore(this.storeName);
      const req = store.clear();
      req.onsuccess = () => resolve();
      req.onerror = (e) => {
        console.error(`Error clearing object store: ${e.target.error}`);
        reject(e.target.error);
      }
    });
  }
}

export default DBManager;
